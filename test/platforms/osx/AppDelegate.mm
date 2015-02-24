//
//  AppDelegate.m
//
//  Created by abstephens on 1/21/15.
//  Copyright (c) 2015 Google. All rights reserved.
//

#import <WebKit/WebKit.h>
#import <JavaScriptCore/JavaScriptCore.h>

#import <dlfcn.h>

#import "AppDelegate.h"
#import "AppProtocol.h"
#import "HalideRuntime.h"

// Test functions are loaded from the app process using a set of strings
// encoded in the loaded HTML page. These functions take no arguments and return
// zero on success. They may use Halide runtime calls to log info, which are
// wired up to output in the WebView.
typedef int (*test_function_t)(void);

@interface AppDelegate ()
@property (retain) NSWindow *window;
@property (retain) WebView *outputView;
@end

@implementation AppDelegate

- (instancetype)init
{
  self = [super init];
  if (self) {
    _window = [[NSWindow alloc] init];
    _outputView = [[WebView alloc] init];
    _database = [[NSMutableDictionary alloc] init];
  }
  return self;
}

- (void)applicationWillFinishLaunching:(NSNotification *)notification {

  // Setup the application protocol handler
  [NSURLProtocol registerClass:[AppProtocol class]];

  // Setup a very basic main menu
  NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
  [[NSApplication sharedApplication] setMainMenu:menu];

  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@""
                                                action:nil
                                         keyEquivalent:@""];

  NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];

  [item setSubmenu:fileMenu];
  [menu addItem:item];

  [fileMenu addItemWithTitle:@"Quit"
                      action:@selector(terminate:)
               keyEquivalent:@"q"];

  // Setup the application window
  [self.window setFrame:CGRectMake(0, 0, 768, 1024) display:NO];
  [self.window setContentView:self.outputView];
  [self.window setStyleMask:self.window.styleMask |
    NSResizableWindowMask |
    NSClosableWindowMask |
    NSMiniaturizableWindowMask |
    NSTitledWindowMask ];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {

  // Setup the main menu
  [self.window makeKeyAndOrderFront:self];

  // Setup the page load delegate
  self.outputView.frameLoadDelegate = self;

  // Load the test document
  NSURL* url = [[NSBundle mainBundle] URLForResource:@"index" withExtension:@"html"];
  [self.outputView.mainFrame loadRequest:[NSURLRequest requestWithURL:url]];
}

// This method is called after the main webpage is loaded. It calls the test
// function that will eventually output to the page via the echo method below.
- (void)webView:(WebView *)sender didFinishLoadForFrame:(WebFrame *)frame
{
  // Obtain a comma delimited list of test functions to call
  NSString* names = [sender stringByEvaluatingJavaScriptFromString:@"AppTestSymbols;"];

  // Check to see if any test symbols were specified
  if (names == nil || names.length == 0) {
    [self echo:@"Define the function getTestSymbols() to return a string containing a comma delimited list of symbols."];
    return;
  }

  // Parse the name list
  for (NSString* name in [names componentsSeparatedByString:@","]) {

    // Attempt to load the symbol
    test_function_t func = (test_function_t)dlsym(RTLD_DEFAULT, name.UTF8String);
    if (!func) {
      [self echo:[NSString stringWithFormat:@"<div class='error'>%@ not found</div>",name]];
      continue;
    }

    // Execute the function
    int result = func();

    [self echo:[NSString stringWithFormat:@"%@ returned %d",name,result]];
  }
}

// This message appends the specified string, which may contain HTML tags to the
// document displayed in the webview.
- (void)echo:(NSString*)message {
  NSString* htmlMessage = [message stringByReplacingOccurrencesOfString:@"\n" withString:@"<br>"];

  htmlMessage = [NSString stringWithFormat:@"echo(\"%@\");",htmlMessage];

  [self.outputView stringByEvaluatingJavaScriptFromString:htmlMessage];
}

@end

extern "C"
void halide_print(void *user_context, const char * message)
{
  AppDelegate* app = [NSApp delegate];
  [app echo:[NSString stringWithCString:message encoding:NSUTF8StringEncoding]];

  NSLog(@"%s",message);
}

extern "C"
void halide_error(void *user_context, const char * message)
{
  AppDelegate* app = [NSApp delegate];
  [app echo:[NSString stringWithFormat:@"<div class='error'>%s</div>",message]];

  NSLog(@"%s",message);
}

extern "C"
int halide_buffer_display(const buffer_t* buffer)
{
  // Convert the buffer_t to an NSImage

  // TODO: This code should check whether or not the data is planar and handle
  // channel types larger than one byte.
  void* data_ptr = buffer->host;

  size_t width            = buffer->extent[0];
  size_t height           = buffer->extent[1];
  size_t channels         = buffer->extent[2];
  size_t bitsPerComponent = buffer->elem_size*8;

  // For planar data, there is one channel across the row
  size_t src_bytesPerRow      = width*buffer->elem_size;
  size_t dst_bytesPerRow      = width*channels*buffer->elem_size;

  size_t totalBytes = width*height*channels*buffer->elem_size;

  // Unlike Mac OS X Cocoa which directly supports planar data via
  // NSBitmapImageRep, in iOS we must create a CGImage from the pixel data and
  // Quartz only supports interleaved formats.
  unsigned char* src_buffer = (unsigned char*)data_ptr;
  unsigned char* dst_buffer = (unsigned char*)malloc(totalBytes);

  // Interleave the data
  for (size_t c=0;c!=buffer->extent[2];++c) {
    for (size_t y=0;y!=buffer->extent[1];++y) {
      for (size_t x=0;x!=buffer->extent[0];++x) {
        size_t src = x + y*src_bytesPerRow + c * (height*src_bytesPerRow);
        size_t dst = c + x*channels + y*dst_bytesPerRow;
        dst_buffer[dst] = src_buffer[src];
      }
    }
  }

  CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, dst_buffer, totalBytes, NULL);
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

  CGImageRef cgImage = CGImageCreate(width,
                                     height,
                                     bitsPerComponent,
                                     bitsPerComponent*channels,
                                     dst_bytesPerRow,
                                     colorSpace,
                                     kCGBitmapByteOrderDefault,
                                     provider,
                                     NULL,
                                     NO,
                                     kCGRenderingIntentDefault);

  NSImage* image = [[NSImage alloc] initWithCGImage:cgImage size:NSZeroSize];

  // Cleanup
  CGImageRelease(cgImage);
  CGColorSpaceRelease(colorSpace);
  CGDataProviderRelease(provider);

  // Convert the NSImage to a png
  NSData* tiffData = [image TIFFRepresentation];
  NSBitmapImageRep* rep = [NSBitmapImageRep imageRepsWithData:tiffData][0];
  NSData* data = [rep representationUsingType:NSPNGFileType properties:nil];

  // Construct a name for the image resource
  static int counter = 0;
  NSString* url = [NSString stringWithFormat:@"%@:///buffer_t%d",kAppProtocolURLScheme,counter++];

  // Add the buffer to the result database
  AppDelegate* app = [NSApp delegate];
  app.database[url] = data;

  // Load the image through a URL
  [app echo:[NSString stringWithFormat:@"<img src='%@'></img>",url]];

  return 0;
}

extern "C"
int halide_buffer_print(const buffer_t* buffer)
{
  NSMutableArray* output = [NSMutableArray array];

  // TODO sort the stride to determine the fastest changing dimension

  // For 2D + color channels images, the third dimension extent is usually zero
  int extent3 = buffer->extent[3] ? buffer->extent[3] : 1;

  for (int z = 0; z != extent3; ++z) {
    for (int y = 0; y != buffer->extent[2]; ++y) {
      for (int x = 0; x != buffer->extent[1]; ++x) {
        for (int c = 0; c != buffer->extent[0]; ++c) {
          int idx = z*buffer->stride[3] + y*buffer->stride[2] + x*buffer->stride[1] + c*buffer->stride[0];
          switch (buffer->elem_size) {
            case 1:
              [output addObject:[NSString stringWithFormat:@"%d,",((unsigned char*)buffer->host)[idx]]];
              break;
            case 4:
              [output addObject:[NSString stringWithFormat:@"%f,",((float*)buffer->host)[idx]]];
              break;
          }
        }
        [output addObject:@"\n"];
      }
      [output addObject:@"\n"];
    }
    [output addObject:@"\n\n"];
  }

  NSString* text = [output componentsJoinedByString:@""];

  // Output the buffer as a string
  AppDelegate* app = [NSApp delegate];
  [app echo:[NSString stringWithFormat:@"<pre class='data'>%@</pre><br>",text]];

  return 0;
}

