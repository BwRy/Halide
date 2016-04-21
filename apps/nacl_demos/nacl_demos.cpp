/// Copyright (c) 2012 The Native Client Authors. All rights reserved.
/// Use of this source code is governed by a BSD-style license that can be
/// found in the LICENSE file.
///
/// @file hello_nacl.cpp
/// This example demonstrates loading, running and scripting a very simple NaCl
/// module.  To load the NaCl module, the browser first looks for the
/// CreateModule() factory method (at the end of this file).  It calls
/// CreateModule() once to load the module code from your .nexe.  After the
/// .nexe code is loaded, CreateModule() is not called again.
///
/// Once the .nexe code is loaded, the browser than calls the CreateInstance()
/// method on the object returned by CreateModule().  It calls CreateInstance()
/// each time it encounters an <embed> tag that references your NaCl module.
///
/// The browser can talk to your NaCl module via the postMessage() Javascript
/// function.  When you call postMessage() on your NaCl module from the browser,
/// this becomes a call to the HandleMessage() method of your pp::Instance
/// subclass.  You can send messages back to the browser by calling the
/// PostMessage() method on your pp::Instance.  Note that these two methods
/// (postMessage() in Javascript and PostMessage() in C++) are asynchronous.
/// This means they return immediately - there is no waiting for the message
/// to be handled.  This has implications in your program design, particularly
/// when mutating property values that are exposed to both the browser and the
/// NaCl module.

#include <cstdio>
#include <string>
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/graphics_2d.h"
#include "ppapi/cpp/image_data.h"
#include "ppapi/cpp/completion_callback.h"
#include "ppapi/cpp/input_event.h"
#include <sys/time.h>
#include <string.h>
#include <sstream>

#include "HalideRuntime.h"

#include "game_of_life_init.h"
#include "game_of_life_update.h"
#include "game_of_life_render.h"

#include "julia_init.h"
#include "julia_update.h"
#include "julia_render.h"

#include "reaction_diffusion_init.h"
#include "reaction_diffusion_update.h"
#include "reaction_diffusion_render.h"

#include "reaction_diffusion_2_init.h"
#include "reaction_diffusion_2_update.h"
#include "reaction_diffusion_2_render.h"

#define WIDTH 1024
#define HEIGHT 1024
#define MARGIN 8

using namespace pp;

bool busy;
void completion_callback(void *data, int32_t flags) {
    fprintf(stderr, "Got a completion callback with data %p flags %d\n", data, flags);
    busy = false;
}

extern "C" void *halide_malloc(void *, size_t);
extern "C" void halide_free(void *, void *);

halide_nd_buffer_t<2> ImageToBuffer(const ImageData &im) {
    halide_nd_buffer_t<2> buf;
    buf.host = (uint8_t *)im.data();
    buf.dim[0] = {0, im.size().width(), 1};
    buf.dim[1] = {0, im.size().height(), im.stride()/4};
    buf.type = halide_type_of<int32_t>();
    return buf;
}

bool pipeline_barfed = false;
static Instance *inst = NULL;
// TODO: use user context instead of globals above...
extern "C" void halide_error(void */* user_context */, const char *msg) {
    if (inst) {
        inst->PostMessage(msg);
        pipeline_barfed = true;
    }
}

/// The Instance class.  One of these exists for each instance of your NaCl
/// module on the web page.  The browser will ask the Module object to create
/// a new Instance for each occurence of the <embed> tag that has these
/// attributes:
///     type="application/x-nacl"
///     src="hello_nacl.nmf"
/// To communicate with the browser, you must override HandleMessage() for
/// receiving messages from the browser, and use PostMessage() to send messages
/// back to the browser.  Note that this interface is asynchronous.
class HalideDemosInstance : public Instance {
public:
    Graphics2D graphics;
    ImageData framebuffer;
    CompletionCallback callback;

    int mouse_x, mouse_y;

    // The state is up-to 3 dimensional. Bounds query mode should set the dimensionality for us.
    halide_nd_buffer_t<2> render_target;
    halide_nd_buffer_t<3> state_1, state_2;

    /// The constructor creates the plugin-side instance.
    /// @param[in] instance the handle to the browser-side plugin instance.
    explicit HalideDemosInstance(PP_Instance instance) :
        Instance(instance),
        graphics(this, Size(WIDTH, HEIGHT), false),
        framebuffer(this, PP_IMAGEDATAFORMAT_BGRA_PREMUL, Size(WIDTH, HEIGHT), false),
        callback(completion_callback, this) {

        printf("HalideDemosInstance constructor\n");
        BindGraphics(graphics);
        RequestInputEvents(PP_INPUTEVENT_CLASS_MOUSE);
        inst = this;
    }

    virtual ~HalideDemosInstance() {
        free(state_1.host);
        free(state_2.host);
    }

    virtual bool HandleInputEvent(const pp::InputEvent &event) {
        if (event.GetType() == PP_INPUTEVENT_TYPE_MOUSEMOVE) {
            pp::MouseInputEvent ev(event);
            Point p = ev.GetPosition();
            mouse_x = p.x();
            mouse_y = p.y();
            return true;
        }
        return false;
    }

    void print_buffer(const halide_buffer_t *b) {
        std::ostringstream oss;
        oss << "Buffer host = " << ((size_t)b->host) << "<br>";
        for (int i = 0; i < b->dimensions; i++) {
            oss << " Dimension " << i << ": "
                << b->dim[i].min << ", "
                << b->dim[i].extent << ", "
                << b->dim[i].stride << "<br>";
        }
        PostMessage(oss.str());

    }

    void alloc_buffer(halide_buffer_t *b) {
        size_t sz = b->type.bytes();
        for (int i = 0; i < b->dimensions; i++) {
            if (b->dim[i].extent) {
                sz *= b->dim[i].extent;
            }
        }
        b->host = (uint8_t *)halide_malloc(NULL, sz);
    }

    void free_buffer(halide_buffer_t *b) {
        if (b->host) {
            halide_free(NULL, b->host);
            b->host = NULL;
        }
    }

    virtual void HandleMessage(const Var& var_message) {

        if (busy) return;
        busy = true;

        render_target = ImageToBuffer(framebuffer);

        static int thread_pool_size = 8;
        static int halide_last_t = 0;
        static int halide_time_weight = 0;
        static int last_demo = -1;

        int demo = 0;

        if (var_message.is_string()) {
            std::string msg = var_message.AsString();
            int threads = atoi(msg.c_str()+2);
            if (threads < 1) threads = 1;
            if (threads > 32) threads = 32;
            if (threads > 0 && threads <= 32 && thread_pool_size != threads) {
                thread_pool_size = threads;
                halide_set_num_threads(thread_pool_size);
                halide_last_t = 0;
                halide_time_weight = 0;
            }
            demo = msg[0] - '0';
        }

        static bool first_run = true;
        if (first_run) {
            first_run = false;
            halide_set_num_threads(thread_pool_size);
        }

        // Useful code for debugging struct layout disagreements between pnacl and Halide.
        if (0) {
            // Print the layout of buffer_t and exit
            std::ostringstream oss;
            oss << "sizeof(halide_buffer_t) = "    << sizeof(halide_buffer_t) << "<br>";
            oss << "sizeof(halide_dimension_t) = " << sizeof(halide_dimension_t) << "<br>";
            oss << "sizeof(halide_type_t) = "      << sizeof(halide_type_t) << "<br>";
            halide_buffer_t b;
            oss << "offset of device = "           << (size_t(&b.device) - size_t(&b)) << "<br>";
            oss << "offset of device_interface = " << (size_t(&b.device_interface) - size_t(&b)) << "<br>";
            oss << "offset of host = "             << (size_t(&b.host) - size_t(&b)) << "<br>";
            oss << "offset of flags = "            << (size_t(&b.flags) - size_t(&b)) << "<br>";
            oss << "offset of type = "             << (size_t(&b.type) - size_t(&b)) << "<br>";
            oss << "offset of dimensions = "       << (size_t(&b.dimensions) - size_t(&b)) << "<br>";
            oss << "offset of dim = "              << (size_t(&b.dim) - size_t(&b)) << "<br>";
            PostMessage(oss.str());
            return;
        }

        // Debugging code for inspecting the buffer.
        if (0) {
            // Print the render target buffer and exit
            print_buffer(&render_target);
            return;
        }

        // Initialize the input
        if (demo != last_demo) {
            last_demo = demo;

            // Delete any existing state
            free_buffer(&state_1);
            free_buffer(&state_2);

            halide_last_t = 0;
            halide_time_weight = 0;

            switch (demo) {
            case 0:
                // Query how large the state arrays need to be in
                // order to hit our render target using Halide's
                // bounds query mode.
                game_of_life_render(&state_1, &render_target);
                state_2 = state_1;
                alloc_buffer(&state_1);
                alloc_buffer(&state_2);
                // Initialize into the first one
                game_of_life_init(&state_1);
                break;
            case 1:
                julia_render(&state_1, &render_target);
                state_2 = state_1;
                alloc_buffer(&state_1);
                alloc_buffer(&state_2);
                julia_init(&state_1);
                break;
            case 2:
                reaction_diffusion_render(&state_1, &render_target);
                state_2 = state_1;
                alloc_buffer(&state_1);
                alloc_buffer(&state_2);
                print_buffer(&state_1);
                reaction_diffusion_init(&state_1);
                break;
            case 3:
                reaction_diffusion_2_render(&state_1, &render_target);
                state_2 = state_1;
                alloc_buffer(&state_1);
                alloc_buffer(&state_2);
                print_buffer(&state_1);
                reaction_diffusion_2_init(&state_1);
                break;
            default:
                PostMessage("Bad demo index");
                return;
            }
        }

        if (pipeline_barfed) {
            return;
        }

        timeval t1, t2;
        gettimeofday(&t1, NULL);
        switch (demo) {
        case 0:
            game_of_life_update(&state_1, mouse_x, mouse_y, &state_2);
            game_of_life_render(&state_2, &render_target);
            break;
        case 1:
            julia_update(&state_1, mouse_x, mouse_y, &state_2);
            julia_render(&state_2, &render_target);
            break;
        case 2:
            reaction_diffusion_update(&state_1, mouse_x, mouse_y, &state_2);
            reaction_diffusion_render(&state_2, &render_target);
            break;
        case 3:
            reaction_diffusion_2_update(&state_1, mouse_x, mouse_y, &state_2);
            reaction_diffusion_2_render(&state_2, &render_target);
            break;
        }
        gettimeofday(&t2, NULL);
        std::swap(state_1, state_2);

        mouse_x = mouse_y = -100;

        if (pipeline_barfed) {
            return;
        }

        int t = t2.tv_usec - t1.tv_usec;
        t += (t2.tv_sec - t1.tv_sec)*1000000;

        // Smooth it out so we can see a rolling average
        t = (halide_last_t * halide_time_weight + t) / (halide_time_weight + 1);
        halide_last_t = t;
        if (halide_time_weight < 100) {
            halide_time_weight++;
        }

        std::ostringstream oss;
        oss << "<table cellspacing=8><tr><td width=200 height=30>Halide routine takes:</td><td>";
        if (halide_time_weight < 10) {
            oss << "?";
        } else {
            oss << halide_last_t;
        }
        oss << " us</td></tr></table>";

        PostMessage(oss.str());

        graphics.PaintImageData(framebuffer, Point(0, 0));
        graphics.Flush(callback);
    }
};

/// The Module class.  The browser calls the CreateInstance() method to create
/// an instance of your NaCl module on the web page.  The browser creates a new
/// instance for each <embed> tag with type="application/x-nacl".
class HalideDemosModule : public Module {
public:
    HalideDemosModule() : Module() {}
    virtual ~HalideDemosModule() {}

    /// Create and return a HalideDemosInstance object.
    /// @param[in] instance The browser-side instance.
    /// @return the plugin-side instance.
    virtual Instance* CreateInstance(PP_Instance instance) {
        return new HalideDemosInstance(instance);
    }
};

namespace pp {
/// Factory function called by the browser when the module is first loaded.
/// The browser keeps a singleton of this module.  It calls the
/// CreateInstance() method on the object you return to make instances.  There
/// is one instance per <embed> tag on the page.  This is the main binding
/// point for your NaCl module with the browser.
Module* CreateModule() {
    return new HalideDemosModule();
}
}  // namespace pp
