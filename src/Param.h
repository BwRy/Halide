#ifndef HALIDE_PARAM_H
#define HALIDE_PARAM_H

/** \file
 *
 * Classes for declaring scalar and image parameters to halide pipelines
 */

#include "IR.h"
#include "Var.h"
#include "IRPrinter.h"
#include "Util.h"
#include <sstream>
#include <vector>

namespace Halide {

/** A scalar parameter to a halide pipeline. If you're jitting, this
 * should be bound to an actual value of type T using the set method
 * before you realize the function uses this. If you're statically
 * compiling, this param should appear in the argument list. */
template<typename T>
class Param {
    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;

public:
    /** Construct a scalar parameter of type T with a unique
     * auto-generated name */
    Param() : param(type_of<T>(), false, Internal::make_entity_name(this, "Halide::Param", 'p')) {}

    /** Construct a scalar parameter of type T with the given name */
    Param(const std::string &n) : param(type_of<T>(), false, n) {}

    /** Get the name of this parameter */
    const std::string &name() const {
        return param.name();
    }

    /** Get the current value of this parameter. Only meaningful when jitting. */
    T get() const {
        return param.get_scalar<T>();
    }

    /** Set the current value of this parameter. Only meaningful when jitting */
    void set(T val) {
        param.set_scalar<T>(val);
    }

    /** Get the halide type of T */
    Type type() const {
        return type_of<T>();
    }

    /** Get or set the possible range of this parameter. Use undefined
     * Exprs to mean unbounded. */
    // @{
    void set_range(Expr min, Expr max) {
        set_min_value(min);
        set_max_value(max);
    }

    void set_min_value(Expr min) {
        if (min.type() != type_of<T>()) {
            min = Internal::Cast::make(type_of<T>(), min);
        }
        param.set_min_value(min);
    }

    void set_max_value(Expr max) {
        if (max.type() != type_of<T>()) {
            max = Internal::Cast::make(type_of<T>(), max);
        }
        param.set_max_value(max);
    }

    Expr get_min_value() {
        return param.get_min_value();
    }

    Expr get_max_value() {
        return param.get_max_value();
    }
    // @}

    /** You can use this parameter as an expression in a halide
     * function definition */
    operator Expr() const {
        return Internal::Variable::make(type_of<T>(), name(), param);
    }

    /** Using a param as the argument to an external stage treats it
     * as an Expr */
    operator ExternFuncArgument() const {
        return Expr(*this);
    }

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    operator Argument() const {
        return Argument(name(), false, type());
    }
};

/** Returns a Param corresponding to a pointer to a user context
 * structure; when the Halide function that takes such a parameter
 * calls a function from the Halide runtime (e.g. halide_printf()), it
 * passes the value of this pointer as the first argument to the
 * runtime function.  */
inline Param<void *> user_context_param() {
  return Param<void *>("__user_context");
}

/** A handle on the output buffer of a pipeline. Used to make static
 * promises about the output size and stride. */
class OutputImageParam {
protected:
    /** A reference-counted handle on the internal parameter object */
    Internal::Parameter param;

    /** The dimensionality of this image. */
    int dims;

    void add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                          Expr last_arg,
                                          int total_args,
                                          bool *placeholder_seen) const {
        const Internal::Variable *var = last_arg.as<Internal::Variable>();
        bool is_placeholder = var && Var::is_placeholder(var->name);
        if (is_placeholder) {
            user_assert(!(*placeholder_seen))
                << "Only one implicit placeholder ('_') allowed in argument list for ImageParam " << name() << "\n";
            *placeholder_seen = true;

            // The + 1 in the conditional is because one provided argument is an placeholder
            for (int i = 0; i < (dims - total_args + 1); i++) {
                args.push_back(Var::implicit(i));
            }
        } else {
            args.push_back(last_arg);
        }


    }

public:

    /** Construct a NULL image parameter handle. */
    OutputImageParam() :
        dims(0) {}

    /** Construct an OutputImageParam that wraps an Internal Parameter object. */
    OutputImageParam(const Internal::Parameter &p, int d) :
        param(p), dims(d) {}

    /** Get the name of this Param */
    const std::string &name() const {
        return param.name();
    }

    /** Get the type of the image data this Param refers to */
    Type type() const {
        return param.type();
    }

    /** Is this parameter handle non-NULL */
    bool defined() {
        return param.defined();
    }

    /** Get an expression representing the minimum coordinates of this image
     * parameter in the given dimension. */
    Expr min(int x) const {
        std::ostringstream s;
        s << name() << ".min." << x;
        return Internal::Variable::make(Int(32), s.str(), param);
    }

    /** Get an expression representing the extent of this image
     * parameter in the given dimension */
    Expr extent(int x) const {
        std::ostringstream s;
        s << name() << ".extent." << x;
        return Internal::Variable::make(Int(32), s.str(), param);
    }

    /** Get an expression representing the stride of this image in the
     * given dimension */
    Expr stride(int x) const {
        std::ostringstream s;
        s << name() << ".stride." << x;
        return Internal::Variable::make(Int(32), s.str(), param);
    }

    /** Set the extent in a given dimension to equal the given
     * expression. Images passed in that fail this check will generate
     * a runtime error. Returns a reference to the ImageParam so that
     * these calls may be chained.
     *
     * This may help the compiler generate better
     * code. E.g:
     \code
     im.set_extent(0, 100);
     \endcode
     * tells the compiler that dimension zero must be of extent 100,
     * which may result in simplification of boundary checks. The
     * value can be an arbitrary expression:
     \code
     im.set_extent(0, im.extent(1));
     \endcode
     * declares that im is a square image (of unknown size), whereas:
     \code
     im.set_extent(0, (im.extent(0)/32)*32);
     \endcode
     * tells the compiler that the extent is a multiple of 32. */
    OutputImageParam &set_extent(int dim, Expr extent) {
        param.set_extent_constraint(dim, extent);
        return *this;
    }

    /** Set the min in a given dimension to equal the given
     * expression. Setting the mins to zero may simplify some
     * addressing math. */
    OutputImageParam &set_min(int dim, Expr min) {
        param.set_min_constraint(dim, min);
        return *this;
    }

    /** Set the stride in a given dimension to equal the given
     * value. This is particularly helpful to set when
     * vectorizing. Known strides for the vectorized dimension
     * generate better code. */
    OutputImageParam &set_stride(int dim, Expr stride) {
        param.set_stride_constraint(dim, stride);
        return *this;
    }

    /** Set the min and extent in one call. */
    OutputImageParam &set_bounds(int dim, Expr min, Expr extent) {
        return set_min(dim, min).set_extent(dim, extent);
    }

    /** Get the dimensionality of this image parameter */
    int dimensions() const {
        return dims;
    }

    /** Get an expression giving the minimum coordinate in dimension 0, which
     * by convention is the coordinate of the left edge of the image */
    Expr left() const {
        user_assert(dims > 0) << "Can't ask for the left of a zero-dimensional image\n";
        return min(0);
    }

    /** Get an expression giving the maximum coordinate in dimension 0, which
     * by convention is the coordinate of the right edge of the image */
    Expr right() const {
        user_assert(dims > 0) << "Can't ask for the right of a zero-dimensional image\n";
        return Internal::Add::make(min(0), Internal::Sub::make(extent(0), 1));
    }

    /** Get an expression giving the minimum coordinate in dimension 1, which
     * by convention is the top of the image */
    Expr top() const {
        user_assert(dims > 1) << "Can't ask for the top of a zero- or one-dimensional image\n";
        return min(1);
    }

    /** Get an expression giving the maximum coordinate in dimension 1, which
     * by convention is the bottom of the image */
    Expr bottom() const {
        user_assert(dims > 1) << "Can't ask for the bottom of a zero- or one-dimensional image\n";
        return Internal::Add::make(min(1), Internal::Sub::make(extent(1), 1));
    }

    /** Get an expression giving the extent in dimension 0, which by
     * convention is the width of the image */
    Expr width() const {
        user_assert(dims > 0) << "Can't ask for the width of a zero-dimensional image\n";
        return extent(0);
    }

    /** Get an expression giving the extent in dimension 1, which by
     * convention is the height of the image */
    Expr height() const {
        user_assert(dims > 1) << "Can't ask for the height of a zero or one-dimensional image\n";
        return extent(1);
    }

    /** Get an expression giving the extent in dimension 2, which by
     * convention is the channel-count of the image */
    Expr channels() const {
        user_assert(dims > 2) << "Can't ask for the channels of an image with fewer than three dimensions\n";
        return extent(2);
    }

    /** Get at the internal parameter object representing this ImageParam. */
    Internal::Parameter parameter() const {
        return param;
    }

    /** Construct the appropriate argument matching this parameter,
     * for the purpose of generating the right type signature when
     * statically compiling halide pipelines. */
    operator Argument() const {
        return Argument(name(), true, type());
    }

    /** Using a param as the argument to an external stage treats it
     * as an Expr */
    operator ExternFuncArgument() const {
        return param;
    }
};

/** An Image parameter to a halide pipeline. E.g., the input image. */
class ImageParam : public OutputImageParam {

public:

    /** Construct a NULL image parameter handle. */
    ImageParam() : OutputImageParam() {}

    /** Construct an image parameter of the given type and
     * dimensionality, with an auto-generated unique name. */
    ImageParam(Type t, int d) :
        OutputImageParam(Internal::Parameter(t, true, Internal::make_entity_name(this, "Halide::ImageParam", 'p')), d) {}

    /** Construct an image parameter of the given type and
     * dimensionality, with the given name */
    ImageParam(Type t, int d, const std::string &n) :
        OutputImageParam(Internal::Parameter(t, true, n), d) {
        // Discourage future Funcs from having the same name
        Internal::unique_name(n);
    }

    /** Bind a buffer or image to this ImageParam. Only relevant for jitting */
    void set(Buffer b) {
        if (b.defined()) {
            user_assert(b.type() == type())
                << "Can't bind ImageParam " << name()
                << " of type " << type()
                << " to Buffer " << b.name()
                << " of type " << b.type() << "\n";
        }
        param.set_buffer(b);
    }

    /** Get the buffer bound to this ImageParam. Only relevant for jitting */
    Buffer get() const {
        return param.get_buffer();
    }

    /** Construct an expression which loads from this image
     * parameter. The location is extended with enough implicit
     * variables to match the dimensionality of the image
     * (see \ref Var::implicit)
     */
    // @{
    Expr operator()() const {
        user_assert(dims == 0)
            << "Zero-argument access to Buffer " << name()
            << ", which has " << dims << "dimensions.\n";
        std::vector<Expr> args;
        return Internal::Call::make(param, args);
    }

    /** Force the args to a call to an image to be int32. */
    static void check_arg_types(const std::string &name, std::vector<Expr> *args, int dims) {
        user_assert(args->size() == (size_t)dims)
            << args->size() << "-argument access to Buffer "
            << name << ", which has " << dims << " dimensions.\n";

        for (size_t i = 0; i < args->size(); i++) {
            Type t = (*args)[i].type();
            if (t.is_float() || (t.is_uint() && t.bits >= 32) || (t.is_int() && t.bits > 32)) {
                user_error << "Error: implicit cast from " << t << " to int in argument " << (i+1)
                           << " in call to " << name << " is not allowed. Use an explicit cast.\n";
            }
            // We're allowed to implicitly cast from other varieties of int
            if (t != Int(32)) {
                (*args)[i] = Internal::Cast::make(Int(32), (*args)[i]);
            }
        }
    }

    Expr operator()(Expr x) const {
        std::vector<Expr> args;
        bool placeholder_seen = false;
        add_implicit_args_if_placeholder(args, x, 1, &placeholder_seen);

        check_arg_types(name(), &args, dims);
        return Internal::Call::make(param, args);
    }

    Expr operator()(Expr x, Expr y) const {
        std::vector<Expr> args;
        bool placeholder_seen = false;
        add_implicit_args_if_placeholder(args, x, 2, &placeholder_seen);
        add_implicit_args_if_placeholder(args, y, 2, &placeholder_seen);

        check_arg_types(name(), &args, dims);
        return Internal::Call::make(param, args);
    }

    Expr operator()(Expr x, Expr y, Expr z) const {
        std::vector<Expr> args;
        bool placeholder_seen = false;
        add_implicit_args_if_placeholder(args, x, 3, &placeholder_seen);
        add_implicit_args_if_placeholder(args, y, 3, &placeholder_seen);
        add_implicit_args_if_placeholder(args, z, 3, &placeholder_seen);

        check_arg_types(name(), &args, dims);
        return Internal::Call::make(param, args);
    }

    Expr operator()(Expr x, Expr y, Expr z, Expr w) const {
        std::vector<Expr> args;
        bool placeholder_seen = false;
        add_implicit_args_if_placeholder(args, x, 4, &placeholder_seen);
        add_implicit_args_if_placeholder(args, y, 4, &placeholder_seen);
        add_implicit_args_if_placeholder(args, z, 4, &placeholder_seen);
        add_implicit_args_if_placeholder(args, w, 4, &placeholder_seen);

        check_arg_types(name(), &args, dims);
        return Internal::Call::make(param, args);
    }
    // @}

    /** Treating the image parameter as an Expr is equivalent to call
     * it with no arguments. For example, you can say:
     *
     \code
     ImageParam im(UInt(8), 2);
     Func f;
     f = im*2;
     \endcode
     *
     * This will define f as a two-dimensional function with value at
     * position (x, y) equal to twice the value of the image parameter
     * at the same location.
     */
    operator Expr() const {
        return (*this)(_);
    }
};


}

#endif
