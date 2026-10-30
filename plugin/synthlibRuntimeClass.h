/*
 * SynthLib - common library for synthesizer editor applications.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __SYNTHLIB_RUNTIME_CLASS_H__
#define __SYNTHLIB_RUNTIME_CLASS_H__

// Obj-C class names are global to a process, and several SynthLib plug-ins share one host. A fixed
// name in the one build of SynthLib would collide, so the views are made at run time instead, each
// named after the image that made it. No application has to supply a name.
//
// The cost is that nothing here may be a static class: the runtime reports a duplicate for any
// @implementation that two plug-ins both contain. Methods are plain C functions, added by
// synthlib_runtime_class_override(); state sits in one ivar, reached with
// synthlib_runtime_class_state().

#import <Cocoa/Cocoa.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

#define SYNTHLIB_RUNTIME_STATE_IVAR    "state"

// A class holding any static one keeps its image loaded; these hold none, so ask for the same thing
// explicitly. The classes outlive every plug-in instance and their methods live in this image.
static inline void synthlib_runtime_class_pin_image(const void * address) {
    Dl_info info = {0};

    if (dladdr(address, &info) != 0) {
        (void)dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
    }
}

static inline Class synthlib_runtime_class_begin(Class superclass, const char * stem, size_t stateSize,
                                                 const void * imageAddress) {
    char  name[96] = {0};
    Class cls      = Nil;

    snprintf(name, sizeof(name), "%s_%llx", stem, (unsigned long long)(uintptr_t)imageAddress);
    cls = objc_allocateClassPair(superclass, name, 0);

    if (cls != Nil) {
        class_addIvar(cls, SYNTHLIB_RUNTIME_STATE_IVAR, stateSize, 3, "?");
    }
    synthlib_runtime_class_pin_image(imageAddress);
    return cls;
}

// Overrides a method of the superclass, taking its type encoding from there.
static inline void synthlib_runtime_class_override(Class cls, SEL selector, void * imp) {
    Method inherited = class_getInstanceMethod(class_getSuperclass(cls), selector);

    class_addMethod(cls, selector, (IMP)imp, method_getTypeEncoding(inherited));
}

// The ivar is zeroed when the object is allocated and goes with it.
static inline void * synthlib_runtime_class_state(Class cls, id object) {
    static ptrdiff_t offset = 0;

    if (offset == 0) {
        offset = ivar_getOffset(class_getInstanceVariable(cls, SYNTHLIB_RUNTIME_STATE_IVAR));
    }
    return (char *)(__bridge void *)object + offset;
}

static inline struct objc_super synthlib_runtime_super(id object, Class superclass) {
    struct objc_super s = {object, superclass};

    return s;
}

#define SYNTHLIB_SUPER_VOID(self, cmd)                                     \
   do {struct objc_super s = synthlib_runtime_super(self, [NSView class]); \
       ((void (*)(struct objc_super *, SEL)) objc_msgSendSuper)(&s, cmd);} while (0)

#define SYNTHLIB_SUPER_ARG(self, cmd, type, arg)                           \
   do {struct objc_super s = synthlib_runtime_super(self, [NSView class]); \
       ((void (*)(struct objc_super *, SEL, type)) objc_msgSendSuper)(&s, cmd, arg);} while (0)

#endif // __SYNTHLIB_RUNTIME_CLASS_H__
