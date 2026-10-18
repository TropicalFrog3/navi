#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>

/*
 * XWayland XkbGetKeyboard Compatibility Shim
 * 
 * Older versions of GLFW statically linked in games (such as The Binding of Isaac: Rebirth)
 * call:
 *     XkbGetKeyboard(display, 0x7f, XkbUseCoreKbd)
 * On modern XWayland servers, querying all components at once in this fashion returns NULL,
 * causing older GLFW to immediately dereference a null pointer when reading min_key_code.
 * 
 * This shim intercepts XkbGetKeyboard, and if NULL is returned, falls back to fetching
 * the client mapping and names via XkbGetMap + XkbGetNames, exactly as modern GLFW does.
 * 
 * Compile:
 *     gcc -O2 -fPIC -shared -o libxkb_fix.so xkb_fix.c -ldl -lX11
 */

typedef XkbDescPtr (*XkbGetKeyboard_fn)(Display *, unsigned int, unsigned int);

XkbDescPtr XkbGetKeyboard(Display *display, unsigned int which, unsigned int device_spec) {
    static XkbGetKeyboard_fn real_fn = NULL;
    if (!real_fn) {
        real_fn = (XkbGetKeyboard_fn)dlsym(RTLD_NEXT, "XkbGetKeyboard");
    }

    XkbDescPtr desc = NULL;
    if (real_fn) {
        desc = real_fn(display, which, device_spec);
    }

    if (!desc) {
        fprintf(stderr, "[xkb_fix] XkbGetKeyboard(which=0x%x, dev=0x%x) returned NULL, applying XWayland fallback\n", which, device_spec);
        desc = XkbGetMap(display, XkbAllClientInfoMask, device_spec);
        if (desc) {
            XkbGetNames(display, XkbAllNamesMask, desc);
        }
    }
    return desc;
}

/*
 * Gamepad Description Normalizer
 *
 * The Binding of Isaac: Rebirth matches gamepad names against a hardcoded
 * internal table using strict strcmp:
 *   - "Microsoft X-Box 360 pad"
 *   - "Generic X-Box pad"
 *   - "Logitech Gamepad F310"
 *   - "Sony Computer Entertainment Wireless Controller"
 *   - "Sony PLAYSTATION(R)3 Controller"
 *
 * Steam's virtual gamepad driver (uinput) appends a device index, e.g.:
 *   "Microsoft X-Box 360 pad 0"
 * This causes strict strcmp to fail, leading Isaac to mark the controller as
 * unmapped (-1 for all buttons and axes) so buttons and analog sticks never register.
 *
 * We intercept ioctl() for both EVIOCGNAME (evdev) and JSIOCGNAME (joydev),
 * normalizing any matching prefix by removing trailing indices.
 */

typedef int (*real_ioctl_fn)(int fd, unsigned long request, void *argp);
static real_ioctl_fn real_ioctl = NULL;

__attribute__((constructor))
static void init_shim(void) {
    if (!real_ioctl) {
        real_ioctl = (real_ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    }
}

int ioctl(int fd, unsigned long request, ...) {
    if (!real_ioctl) {
        real_ioctl = (real_ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    }

    va_list args;
    va_start(args, request);
    void *argp = va_arg(args, void *);
    va_end(args);

    int ret = real_ioctl(fd, request, argp);
    if (ret < 0) {
        return ret;
    }

    unsigned int type = _IOC_TYPE(request);
    unsigned int nr   = _IOC_NR(request);

    if ((type == 'E' && nr == 0x06) || (type == 'j' && nr == 0x13)) {
        if (argp != NULL) {
            char *name = (char *)argp;
            static const char * const known_gamepads[] = {
                "Microsoft X-Box 360 pad",
                "Generic X-Box pad",
                "Logitech Gamepad F310",
                "Sony Computer Entertainment Wireless Controller",
                "Sony PLAYSTATION(R)3 Controller",
                NULL
            };

            for (int i = 0; known_gamepads[i] != NULL; i++) {
                size_t len = strlen(known_gamepads[i]);
                if (strncmp(name, known_gamepads[i], len) == 0) {
                    fprintf(stderr, "[xkb_fix] Normalizing gamepad name: \"%s\" -> \"%s\"\n",
                            name, known_gamepads[i]);
                    strcpy(name, known_gamepads[i]);
                    return (int)len;
                }
            }

            /* Fallback: if it's an unrecognized Xbox or Steam virtual pad, normalize to Microsoft X-Box 360 pad */
            if (strstr(name, "X-Box") || strstr(name, "Xbox") || strstr(name, "Steam Virtual Gamepad")) {
                fprintf(stderr, "[xkb_fix] Normalizing generic gamepad: \"%s\" -> \"Microsoft X-Box 360 pad\"\n", name);
                strcpy(name, "Microsoft X-Box 360 pad");
                return (int)strlen("Microsoft X-Box 360 pad");
            }
        }
    }

    return ret;
}
