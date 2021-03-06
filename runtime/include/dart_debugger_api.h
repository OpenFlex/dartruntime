// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef INCLUDE_DART_DEBUGGER_API_H_
#define INCLUDE_DART_DEBUGGER_API_H_

#include "include/dart_api.h"

typedef struct _Dart_Breakpoint* Dart_Breakpoint;

typedef struct _Dart_StackTrace* Dart_StackTrace;

typedef struct _Dart_ActivationFrame* Dart_ActivationFrame;

typedef void Dart_BreakpointHandler(
                 Dart_Breakpoint breakpoint,
                 Dart_StackTrace stack_trace);


/**
 * Returns a list of urls (strings) of all the libraries loaded in the
 * current isolate.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to a list of string handles.
 */
DART_EXPORT Dart_Handle Dart_GetLibraryURLs();


/**
 * Returns a list of urls (strings) of all the scripts loaded in the
 * given library.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to a list of string handles.
 */
DART_EXPORT Dart_Handle Dart_GetScriptURLs(Dart_Handle library_url);


/**
 * Returns a string containing the source code of the given script
 * in the given library.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to string containing the source text if no error
 * occurs.
 */
DART_EXPORT Dart_Handle Dart_GetScriptSource(
                            Dart_Handle library_url_in,
                            Dart_Handle script_url_in);

/**
 * Sets a breakpoint at line \line_number in \script_url, or the closest
 * following line (within the same function) where a breakpoint can be set.
 *
 * Requires there to be a current isolate.
 *
 * \breakpoint If non-null, will point to the breakpoint object
 *   if a breakpoint was successfully created.
 *
 * \return A handle to the True object if no error occurs.
 */
DART_EXPORT Dart_Handle Dart_SetBreakpointAtLine(
                            Dart_Handle script_url,
                            Dart_Handle line_number,
                            Dart_Breakpoint* breakpoint);


/**
 * Sets a breakpoint at the entry of the given function. If class_name
 * is the empty string, looks for a library function with the given
 * name.
 *
 * Requires there to be a current isolate.
 *
 * \breakpoint If non-null, will point to the breakpoint object
 *   if a breakpoint was successfully created.
 *
 * \return A handle to the True object if no error occurs.
 */
DART_EXPORT Dart_Handle Dart_SetBreakpointAtEntry(
                            Dart_Handle library,
                            Dart_Handle class_name,
                            Dart_Handle function_name,
                            Dart_Breakpoint* breakpoint);


/**
 * Deletes the given \breakpoint.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to the True object if no error occurs.
 */
DART_EXPORT Dart_Handle Dart_DeleteBreakpoint(
                            Dart_Breakpoint breakpoint);


/**
 * Can be called from the breakpoint handler. Sets the debugger to
 * single step mode.
 *
 * Requires there to be a current isolate.
 */
DART_EXPORT Dart_Handle Dart_SetStepOver();


/**
 * Can be called from the breakpoint handler. Causes the debugger to
 * break after at the beginning of the next function call.
 *
 * Requires there to be a current isolate.
 */
DART_EXPORT Dart_Handle Dart_SetStepInto();


/**
 * Can be called from the breakpoint handler. Causes the debugger to
 * break after returning from the current Dart function.
 *
 * Requires there to be a current isolate.
 */
DART_EXPORT Dart_Handle Dart_SetStepOut();


/**
 * Installs a handler callback function that gets called by the VM
 * when a breakpoint has been reached.
 *
 * Requires there to be a current isolate.
 */
DART_EXPORT void Dart_SetBreakpointHandler(
                            Dart_BreakpointHandler bp_handler);


/**
 * Returns in \length the number of activation frames in the given
 * stack trace.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to the True object if no error occurs.
 */
DART_EXPORT Dart_Handle Dart_StackTraceLength(
                            Dart_StackTrace trace,
                            intptr_t* length);


/**
 * Returns in \frame the activation frame with index \frame_index.
 * The activation frame at the top of stack has index 0.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to the True object if no error occurs.
 */
DART_EXPORT Dart_Handle Dart_GetActivationFrame(
                            Dart_StackTrace trace,
                            int frame_index,
                            Dart_ActivationFrame* frame);


/**
 * Returns information about the given activation frame.
 * \function_name receives a string handle with the qualified
 *    function name.
 * \script_url receives a string handle with the url of the
 *    source script that contains the frame's function.
 * \line_number receives the line number in the script.
 *
 * Any or all of the out parameters above may be NULL.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to the True object if no error occurs.
 */
DART_EXPORT Dart_Handle Dart_ActivationFrameInfo(
                            Dart_ActivationFrame activation_frame,
                            Dart_Handle* function_name,
                            Dart_Handle* script_url,
                            intptr_t* line_number);


/**
 * Returns an array containing all the local variable names and values of
 * the given \activation_frame.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to an array containing variable names and
 * corresponding values. The array is empty if the activation frame has
 * no variables. If non-empty, variable names are at array offsets 2*n,
 * values at offset 2*n+1.
 */
DART_EXPORT Dart_Handle Dart_GetLocalVariables(
                            Dart_ActivationFrame activation_frame);


/**
 * Returns the class of the given \object.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to the class object.
 */
DART_EXPORT Dart_Handle Dart_GetObjClass(Dart_Handle object);


/**
 * Returns the superclass of the given class \cls.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to the class object.
 */
DART_EXPORT Dart_Handle Dart_GetSuperclass(Dart_Handle cls);


/**
 * Returns an array containing all instance field names and values of
 * the given \object.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to an array containing field names and
 * corresponding field values. The array is empty if the object has
 * no fields. If non-empty, field names are at array offsets 2*n,
 * values at offset 2*n+1. Field values may also be a handle to an
 * error object if an error was encountered evaluating the field.
 */
DART_EXPORT Dart_Handle Dart_GetInstanceFields(Dart_Handle object);


/**
 * Returns an array containing all static field names and values of
 * the given class \cls.
 *
 * Requires there to be a current isolate.
 *
 * \return A handle to an array containing field names and
 * corresponding field values. The array is empty if the class has
 * no static fields. If non-empty, field names are at array offsets 2*n,
 * values at offset 2*n+1. Field values may also be a handle to an
 * error object if an error was encountered evaluating the field.
 */
DART_EXPORT Dart_Handle Dart_GetStaticFields(Dart_Handle cls);


#endif  // INCLUDE_DART_DEBUGGER_API_H_
