/*
 * V3 input bridge implementation.
 *
 * Status:
 * - Active development
 * - Works with some bugs:
 *  + Modded versions gives broken stuff..
 *
 * 
 * - Implements glfwSetCursorPos() to handle grab camera pos correctly.
 */
 
#include <assert.h>
#include <dlfcn.h>
#include <jni.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>

#include "log.h"
#include "utils.h"
#include "environ/environ.h"

#define EVENT_TYPE_CHAR 1000
#define EVENT_TYPE_CHAR_MODS 1001
#define EVENT_TYPE_CURSOR_ENTER 1002
#define EVENT_TYPE_FRAMEBUFFER_SIZE 1004
#define EVENT_TYPE_KEY 1005
#define EVENT_TYPE_MOUSE_BUTTON 1006
#define EVENT_TYPE_SCROLL 1007
#define EVENT_TYPE_WINDOW_SIZE 1008

jint (*orig_ProcessImpl_forkAndExec)(JNIEnv *env, jobject process, jint mode, jbyteArray helperpath, jbyteArray prog, jbyteArray argBlock, jint argc, jbyteArray envBlock, jint envc, jbyteArray dir, jintArray std_fds, jboolean redirectErrorStream);

static void registerFunctions(JNIEnv *env);

jint JNI_OnLoad(JavaVM* vm, __attribute__((unused)) void* reserved) {
    if (solcraft_environ->dalvikJavaVMPtr == NULL) {
        __android_log_print(ANDROID_LOG_INFO, "Native", "Saving DVM environ...");
        //Save dalvik global JavaVM pointer
        solcraft_environ->dalvikJavaVMPtr = vm;
        (*vm)->GetEnv(vm, (void**) &solcraft_environ->dalvikJNIEnvPtr_ANDROID, JNI_VERSION_1_4);
        solcraft_environ->bridgeClazz = (*solcraft_environ->dalvikJNIEnvPtr_ANDROID)->NewGlobalRef(solcraft_environ->dalvikJNIEnvPtr_ANDROID,(*solcraft_environ->dalvikJNIEnvPtr_ANDROID) ->FindClass(solcraft_environ->dalvikJNIEnvPtr_ANDROID,"org/lwjgl/glfw/CallbackBridge"));
        solcraft_environ->method_accessAndroidClipboard = (*solcraft_environ->dalvikJNIEnvPtr_ANDROID)->GetStaticMethodID(solcraft_environ->dalvikJNIEnvPtr_ANDROID, solcraft_environ->bridgeClazz, "accessAndroidClipboard", "(ILjava/lang/String;)Ljava/lang/String;");
        solcraft_environ->method_onGrabStateChanged = (*solcraft_environ->dalvikJNIEnvPtr_ANDROID)->GetStaticMethodID(solcraft_environ->dalvikJNIEnvPtr_ANDROID, solcraft_environ->bridgeClazz, "onGrabStateChanged", "(Z)V");
        solcraft_environ->isUseStackQueueCall = JNI_FALSE;
    } else if (solcraft_environ->dalvikJavaVMPtr != vm) {
        __android_log_print(ANDROID_LOG_INFO, "Native", "Saving JVM environ...");
        solcraft_environ->runtimeJavaVMPtr = vm;
        (*vm)->GetEnv(vm, (void**) &solcraft_environ->runtimeJNIEnvPtr_JRE, JNI_VERSION_1_4);
        solcraft_environ->vmGlfwClass = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->NewGlobalRef(solcraft_environ->runtimeJNIEnvPtr_JRE, (*solcraft_environ->runtimeJNIEnvPtr_JRE)->FindClass(solcraft_environ->runtimeJNIEnvPtr_JRE, "org/lwjgl/glfw/GLFW"));
        solcraft_environ->method_glftSetWindowAttrib = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->GetStaticMethodID(solcraft_environ->runtimeJNIEnvPtr_JRE, solcraft_environ->vmGlfwClass, "glfwSetWindowAttrib", "(JII)V");
        solcraft_environ->method_internalWindowSizeChanged = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->GetStaticMethodID(solcraft_environ->runtimeJNIEnvPtr_JRE, solcraft_environ->vmGlfwClass, "internalWindowSizeChanged", "(JII)V");
        jfieldID field_keyDownBuffer = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->GetStaticFieldID(solcraft_environ->runtimeJNIEnvPtr_JRE, solcraft_environ->vmGlfwClass, "keyDownBuffer", "Ljava/nio/ByteBuffer;");
        jobject keyDownBufferJ = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->GetStaticObjectField(solcraft_environ->runtimeJNIEnvPtr_JRE, solcraft_environ->vmGlfwClass, field_keyDownBuffer);
        solcraft_environ->keyDownBuffer = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->GetDirectBufferAddress(solcraft_environ->runtimeJNIEnvPtr_JRE, keyDownBufferJ);
        jfieldID field_mouseDownBuffer = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->GetStaticFieldID(solcraft_environ->runtimeJNIEnvPtr_JRE, solcraft_environ->vmGlfwClass, "mouseDownBuffer", "Ljava/nio/ByteBuffer;");
        jobject mouseDownBufferJ = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->GetStaticObjectField(solcraft_environ->runtimeJNIEnvPtr_JRE, solcraft_environ->vmGlfwClass, field_mouseDownBuffer);
        solcraft_environ->mouseDownBuffer = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->GetDirectBufferAddress(solcraft_environ->runtimeJNIEnvPtr_JRE, mouseDownBufferJ);
        hookExec();
        installLinkerBugMitigation();
        installEMUIIteratorMititgation();
    }

    if(solcraft_environ->dalvikJavaVMPtr == vm) {
        //perform in all DVM instances, not only during first ever set up
        JNIEnv *env;
        (*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4);
        registerFunctions(env);
    }
    solcraft_environ->isGrabbing = JNI_FALSE;
    
    return JNI_VERSION_1_4;
}

#define ADD_CALLBACK_WWIN(NAME) \
JNIEXPORT jlong JNICALL Java_org_lwjgl_glfw_GLFW_nglfwSet##NAME##Callback(JNIEnv * env, jclass cls, jlong window, jlong callbackptr) { \
    void** oldCallback = (void**) &solcraft_environ->GLFW_invoke_##NAME; \
    solcraft_environ->GLFW_invoke_##NAME = (GLFW_invoke_##NAME##_func*) (uintptr_t) callbackptr; \
    return (jlong) (uintptr_t) *oldCallback; \
}

ADD_CALLBACK_WWIN(Char)
ADD_CALLBACK_WWIN(CharMods)
ADD_CALLBACK_WWIN(CursorEnter)
ADD_CALLBACK_WWIN(CursorPos)
ADD_CALLBACK_WWIN(FramebufferSize)
ADD_CALLBACK_WWIN(Key)
ADD_CALLBACK_WWIN(MouseButton)
ADD_CALLBACK_WWIN(Scroll)
ADD_CALLBACK_WWIN(WindowSize)

#undef ADD_CALLBACK_WWIN

void handleFramebufferSizeJava(long window, int w, int h) {
    (*solcraft_environ->runtimeJNIEnvPtr_JRE)->CallStaticVoidMethod(solcraft_environ->runtimeJNIEnvPtr_JRE, solcraft_environ->vmGlfwClass, solcraft_environ->method_internalWindowSizeChanged, (long)window, w, h);
}

void pojavPumpEvents(void* window) {
    if(solcraft_environ->shouldUpdateMouse) {
        solcraft_environ->GLFW_invoke_CursorPos(window, floor(solcraft_environ->cursorX),
                                             floor(solcraft_environ->cursorY));
    }

    size_t index = solcraft_environ->outEventIndex;
    size_t targetIndex = solcraft_environ->outTargetIndex;

    while (targetIndex != index) {
        GLFWInputEvent event = solcraft_environ->events[index];
        switch (event.type) {
            case EVENT_TYPE_CHAR:
                if(solcraft_environ->GLFW_invoke_Char) solcraft_environ->GLFW_invoke_Char(window, event.i1);
                break;
            case EVENT_TYPE_CHAR_MODS:
                if(solcraft_environ->GLFW_invoke_CharMods) solcraft_environ->GLFW_invoke_CharMods(window, event.i1, event.i2);
                break;
            case EVENT_TYPE_KEY:
                if(solcraft_environ->GLFW_invoke_Key) solcraft_environ->GLFW_invoke_Key(window, event.i1, event.i2, event.i3, event.i4);
                break;
            case EVENT_TYPE_MOUSE_BUTTON:
                if(solcraft_environ->GLFW_invoke_MouseButton) solcraft_environ->GLFW_invoke_MouseButton(window, event.i1, event.i2, event.i3);
                break;
            case EVENT_TYPE_SCROLL:
                if(solcraft_environ->GLFW_invoke_Scroll) solcraft_environ->GLFW_invoke_Scroll(window, event.i1, event.i2);
                break;
            case EVENT_TYPE_FRAMEBUFFER_SIZE:
                handleFramebufferSizeJava(solcraft_environ->showingWindow, event.i1, event.i2);
                if(solcraft_environ->GLFW_invoke_FramebufferSize) solcraft_environ->GLFW_invoke_FramebufferSize(window, event.i1, event.i2);
                break;
            case EVENT_TYPE_WINDOW_SIZE:
                handleFramebufferSizeJava(solcraft_environ->showingWindow, event.i1, event.i2);
                if(solcraft_environ->GLFW_invoke_WindowSize) solcraft_environ->GLFW_invoke_WindowSize(window, event.i1, event.i2);
                break;
        }

        index++;
        if (index >= EVENT_WINDOW_SIZE)
            index -= EVENT_WINDOW_SIZE;
    }

    // The out target index is updated by the rewinder
}

/** Prepare the library for sending out callbacks to all windows */
void pojavStartPumping() {
    size_t counter = atomic_load_explicit(&solcraft_environ->eventCounter, memory_order_acquire);
    size_t index = solcraft_environ->outEventIndex;

    unsigned targetIndex = index + counter;
    if (targetIndex >= EVENT_WINDOW_SIZE)
        targetIndex -= EVENT_WINDOW_SIZE;

    // Only accessed by one unique thread, no need for atomic store
    solcraft_environ->inEventCount = counter;
    solcraft_environ->outTargetIndex = targetIndex;

    //PumpEvents is called for every window, so this logic should be there in order to correctly distribute events to all windows.
    if((solcraft_environ->cLastX != solcraft_environ->cursorX || solcraft_environ->cLastY != solcraft_environ->cursorY) && solcraft_environ->GLFW_invoke_CursorPos) {
        solcraft_environ->cLastX = solcraft_environ->cursorX;
        solcraft_environ->cLastY = solcraft_environ->cursorY;
        solcraft_environ->shouldUpdateMouse = true;
    }
}

/** Prepare the library for the next round of new events */
void pojavStopPumping() {
    solcraft_environ->outEventIndex = solcraft_environ->outTargetIndex;

    // New events may have arrived while pumping, so remove only the difference before the start and end of execution
    atomic_fetch_sub_explicit(&solcraft_environ->eventCounter, solcraft_environ->inEventCount, memory_order_acquire);
    // Make sure the next frame won't send mouse updates if it's unnecessary
    solcraft_environ->shouldUpdateMouse = false;
}

JNIEXPORT void JNICALL
Java_org_lwjgl_glfw_GLFW_nglfwGetCursorPos(JNIEnv *env, __attribute__((unused)) jclass clazz, __attribute__((unused)) jlong window, jobject xpos,
                                          jobject ypos) {
    *(double*)(*env)->GetDirectBufferAddress(env, xpos) = solcraft_environ->cursorX;
    *(double*)(*env)->GetDirectBufferAddress(env, ypos) = solcraft_environ->cursorY;
}

JNIEXPORT void JNICALL JavaCritical_org_lwjgl_glfw_GLFW_nglfwGetCursorPosA(__attribute__((unused)) jlong window, jint lengthx, jdouble* xpos, jint lengthy, jdouble* ypos) {
    *xpos = solcraft_environ->cursorX;
    *ypos = solcraft_environ->cursorY;
}

JNIEXPORT void JNICALL
Java_org_lwjgl_glfw_GLFW_nglfwGetCursorPosA(JNIEnv *env, __attribute__((unused)) jclass clazz, __attribute__((unused)) jlong window,
                                            jdoubleArray xpos, jdoubleArray ypos) {
    (*env)->SetDoubleArrayRegion(env, xpos, 0,1, &solcraft_environ->cursorX);
    (*env)->SetDoubleArrayRegion(env, ypos, 0,1, &solcraft_environ->cursorY);
}

JNIEXPORT void JNICALL JavaCritical_org_lwjgl_glfw_GLFW_glfwSetCursorPos(__attribute__((unused)) jlong window, jdouble xpos,
                                                                         jdouble ypos) {
    solcraft_environ->cLastX = solcraft_environ->cursorX = xpos;
    solcraft_environ->cLastY = solcraft_environ->cursorY = ypos;
}

JNIEXPORT void JNICALL
Java_org_lwjgl_glfw_GLFW_glfwSetCursorPos(__attribute__((unused)) JNIEnv *env, __attribute__((unused)) jclass clazz, __attribute__((unused)) jlong window, jdouble xpos,
                                          jdouble ypos) {
    JavaCritical_org_lwjgl_glfw_GLFW_glfwSetCursorPos(window, xpos, ypos);
}



void sendData(int type, int i1, int i2, int i3, int i4) {
    GLFWInputEvent *event = &solcraft_environ->events[solcraft_environ->inEventIndex];
    event->type = type;
    event->i1 = i1;
    event->i2 = i2;
    event->i3 = i3;
    event->i4 = i4;

    if (++solcraft_environ->inEventIndex >= EVENT_WINDOW_SIZE)
        solcraft_environ->inEventIndex -= EVENT_WINDOW_SIZE;

    atomic_fetch_add_explicit(&solcraft_environ->eventCounter, 1, memory_order_acquire);
}

/**
 * Hooked version of java.lang.UNIXProcess.forkAndExec()
 * which is used to handle the "open" command.
 */
jint
hooked_ProcessImpl_forkAndExec(JNIEnv *env, jobject process, jint mode, jbyteArray helperpath, jbyteArray prog, jbyteArray argBlock, jint argc, jbyteArray envBlock, jint envc, jbyteArray dir, jintArray std_fds, jboolean redirectErrorStream) {
    char *pProg = (char *)((*env)->GetByteArrayElements(env, prog, NULL));

    // Here we only handle the "xdg-open" command
    if (strcmp(basename(pProg), "xdg-open") != 0) {
        (*env)->ReleaseByteArrayElements(env, prog, (jbyte *)pProg, 0);
        return orig_ProcessImpl_forkAndExec(env, process, mode, helperpath, prog, argBlock, argc, envBlock, envc, dir, std_fds, redirectErrorStream);
    }
    (*env)->ReleaseByteArrayElements(env, prog, (jbyte *)pProg, 0);

    Java_org_lwjgl_glfw_CallbackBridge_nativeClipboard(env, NULL, /* CLIPBOARD_OPEN */ 2002, argBlock);
    return 0;
}

void hookExec() {
    jclass cls;
    orig_ProcessImpl_forkAndExec = dlsym(RTLD_DEFAULT, "Java_java_lang_UNIXProcess_forkAndExec");
    if (!orig_ProcessImpl_forkAndExec) {
        orig_ProcessImpl_forkAndExec = dlsym(RTLD_DEFAULT, "Java_java_lang_ProcessImpl_forkAndExec");
        cls = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->FindClass(solcraft_environ->runtimeJNIEnvPtr_JRE, "java/lang/ProcessImpl");
    } else {
        cls = (*solcraft_environ->runtimeJNIEnvPtr_JRE)->FindClass(solcraft_environ->runtimeJNIEnvPtr_JRE, "java/lang/UNIXProcess");
    }
    JNINativeMethod methods[] = {
        {"forkAndExec", "(I[B[B[BI[BI[B[IZ)I", (void *)&hooked_ProcessImpl_forkAndExec}
    };
    (*solcraft_environ->runtimeJNIEnvPtr_JRE)->RegisterNatives(solcraft_environ->runtimeJNIEnvPtr_JRE, cls, methods, 1);
    printf("Registered forkAndExec\n");
}

/**
 * Basically a verbatim implementation of ndlopen(), found at
 * https://github.com/PojavLauncherTeam/lwjgl3/blob/3.3.1/modules/lwjgl/core/src/generated/c/linux/org_lwjgl_system_linux_DynamicLinkLoader.c#L11
 * The idea is that since, on Android 10 and earlier, the linker doesn't really do namespace nesting.
 * It is not a problem as most of the libraries are in the launcher path, but when you try to run
 * VulkanMod which loads shaderc outside of the default jni libs directory through this method,
 * it can't load it because the path is not in the allowed paths for the anonymous namesapce.
 * This method fixes the issue by being in libpojavexec, and thus being in the classloader namespace
 */
jlong ndlopen_bugfix(__attribute__((unused)) JNIEnv *env,
                     __attribute__((unused)) jclass class,
                     jlong filename_ptr,
                     jint jmode) {
    const char* filename = (const char*) filename_ptr;
    int mode = (int)jmode;
    return (jlong) dlopen(filename, mode);
}

/**
 * Install the linker bug mitigation for Android 10 and lower. Fixes VulkanMod crashing on these
 * Android versions due to missing namespace nesting.
 */
void installLinkerBugMitigation() {
    if(android_get_device_api_level() >= 30) return;
    __android_log_print(ANDROID_LOG_INFO, "Api29LinkerFix", "API < 30 detected, installing linker bug mitigation");
    JNIEnv* env = solcraft_environ->runtimeJNIEnvPtr_JRE;
    jclass dynamicLinkLoader = (*env)->FindClass(env, "org/lwjgl/system/linux/DynamicLinkLoader");
    if(dynamicLinkLoader == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "Api29LinkerFix", "Failed to find the target class");
        (*env)->ExceptionClear(env);
        return;
    }
    JNINativeMethod ndlopenMethod[] = {
            {"ndlopen", "(JI)J", &ndlopen_bugfix}
    };
    if((*env)->RegisterNatives(env, dynamicLinkLoader, ndlopenMethod, 1) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "Api29LinkerFix", "Failed to register the bugfix method");
        (*env)->ExceptionClear(env);
    }
}

/**
 * This function is meant as a substitute for SharedLibraryUtil.getLibraryPath() that just returns 0
 * (thus making the parent Java function return null). This is done to avoid using the LWJGL's default function,
 * which will hang the crappy EMUI linker by dlopen()ing inside of dl_iterate_phdr().
 * @return 0, to make the parent Java function return null immediately.
 * For reference: https://github.com/PojavLauncherTeam/lwjgl3/blob/fix_huawei_hang/modules/lwjgl/core/src/main/java/org/lwjgl/system/SharedLibraryUtil.java
 */
jint getLibraryPath_fix(__attribute__((unused)) JNIEnv *env,
                        __attribute__((unused)) jclass class,
                        __attribute__((unused)) jlong pLibAddress,
                        __attribute__((unused)) jlong sOutAddress,
                        __attribute__((unused)) jint bufSize){
    return 0;
}

/**
 * Install the linker hang mitigation that is meant to prevent linker hangs on old EMUI firmware.
 */
void installEMUIIteratorMititgation() {
    if(getenv("POJAV_EMUI_ITERATOR_MITIGATE") == NULL) return;
    __android_log_print(ANDROID_LOG_INFO, "EMUIIteratorFix", "Installing...");
    JNIEnv* env = solcraft_environ->runtimeJNIEnvPtr_JRE;
    jclass sharedLibraryUtil = (*env)->FindClass(env, "org/lwjgl/system/SharedLibraryUtil");
    if(sharedLibraryUtil == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "EMUIIteratorFix", "Failed to find the target class");
        (*env)->ExceptionClear(env);
        return;
    }
    JNINativeMethod getLibraryPathMethod[] = {
            {"getLibraryPath", "(JJI)I", &getLibraryPath_fix}
    };
    if((*env)->RegisterNatives(env, sharedLibraryUtil, getLibraryPathMethod, 1) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "EMUIIteratorFix", "Failed to register the mitigation method");
        (*env)->ExceptionClear(env);
    }
}

void critical_set_stackqueue(jboolean use_input_stack_queue) {
    solcraft_environ->isUseStackQueueCall = (int) use_input_stack_queue;
}

void noncritical_set_stackqueue(__attribute__((unused)) JNIEnv *env, __attribute__((unused)) jclass clazz, jboolean use_input_stack_queue) {
    critical_set_stackqueue(use_input_stack_queue);
}

JNIEXPORT jstring JNICALL Java_org_lwjgl_glfw_CallbackBridge_nativeClipboard(JNIEnv* env, __attribute__((unused)) jclass clazz, jint action, jbyteArray copySrc) {
#ifdef DEBUG
    LOGD("Debug: Clipboard access is going on\n", solcraft_environ->isUseStackQueueCall);
#endif

    JNIEnv *dalvikEnv;
    (*solcraft_environ->dalvikJavaVMPtr)->AttachCurrentThread(solcraft_environ->dalvikJavaVMPtr, &dalvikEnv, NULL);
    assert(dalvikEnv != NULL);
    assert(solcraft_environ->bridgeClazz != NULL);
    
    LOGD("Clipboard: Converting string\n");
    char *copySrcC;
    jstring copyDst = NULL;
    if (copySrc) {
        copySrcC = (char *)((*env)->GetByteArrayElements(env, copySrc, NULL));
        copyDst = (*dalvikEnv)->NewStringUTF(dalvikEnv, copySrcC);
    }

    LOGD("Clipboard: Calling 2nd\n");
    jstring pasteDst = convertStringJVM(dalvikEnv, env, (jstring) (*dalvikEnv)->CallStaticObjectMethod(dalvikEnv, solcraft_environ->bridgeClazz, solcraft_environ->method_accessAndroidClipboard, action, copyDst));

    if (copySrc) {
        (*dalvikEnv)->DeleteLocalRef(dalvikEnv, copyDst);    
        (*env)->ReleaseByteArrayElements(env, copySrc, (jbyte *)copySrcC, 0);
    }
    (*solcraft_environ->dalvikJavaVMPtr)->DetachCurrentThread(solcraft_environ->dalvikJavaVMPtr);
    return pasteDst;
}

JNIEXPORT jboolean JNICALL JavaCritical_org_lwjgl_glfw_CallbackBridge_nativeSetInputReady(jboolean inputReady) {
#ifdef DEBUG
    LOGD("Debug: Changing input state, isReady=%d, solcraft_environ->isUseStackQueueCall=%d\n", inputReady, solcraft_environ->isUseStackQueueCall);
#endif
    __android_log_print(ANDROID_LOG_INFO, "NativeInput", "Input ready: %i", inputReady);
    solcraft_environ->isInputReady = inputReady;
    return solcraft_environ->isUseStackQueueCall;
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_glfw_CallbackBridge_nativeSetInputReady(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jboolean inputReady) {
    return JavaCritical_org_lwjgl_glfw_CallbackBridge_nativeSetInputReady(inputReady);
}

JNIEXPORT void JNICALL Java_org_lwjgl_glfw_CallbackBridge_nativeSetGrabbing(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jboolean grabbing) {
    JNIEnv *dalvikEnv;
    (*solcraft_environ->dalvikJavaVMPtr)->AttachCurrentThread(solcraft_environ->dalvikJavaVMPtr, &dalvikEnv, NULL);
    (*dalvikEnv)->CallStaticVoidMethod(dalvikEnv, solcraft_environ->bridgeClazz, solcraft_environ->method_onGrabStateChanged, grabbing);
    (*solcraft_environ->dalvikJavaVMPtr)->DetachCurrentThread(solcraft_environ->dalvikJavaVMPtr);
    solcraft_environ->isGrabbing = grabbing;
}

jboolean critical_send_char(jchar codepoint) {
    if (solcraft_environ->GLFW_invoke_Char && solcraft_environ->isInputReady) {
        if (solcraft_environ->isUseStackQueueCall) {
            sendData(EVENT_TYPE_CHAR, codepoint, 0, 0, 0);
        } else {
            solcraft_environ->GLFW_invoke_Char((void*) solcraft_environ->showingWindow, (unsigned int) codepoint);
        }
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

jboolean noncritical_send_char(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jchar codepoint) {
    return critical_send_char(codepoint);
}

jboolean critical_send_char_mods(jchar codepoint, jint mods) {
    if (solcraft_environ->GLFW_invoke_CharMods && solcraft_environ->isInputReady) {
        if (solcraft_environ->isUseStackQueueCall) {
            sendData(EVENT_TYPE_CHAR_MODS, (int) codepoint, mods, 0, 0);
        } else {
            solcraft_environ->GLFW_invoke_CharMods((void*) solcraft_environ->showingWindow, codepoint, mods);
        }
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

jboolean noncritical_send_char_mods(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jchar codepoint, jint mods) {
    return critical_send_char_mods(codepoint, mods);
}
/*
JNIEXPORT void JNICALL Java_org_lwjgl_glfw_CallbackBridge_nativeSendCursorEnter(JNIEnv* env, jclass clazz, jint entered) {
    if (solcraft_environ->GLFW_invoke_CursorEnter && solcraft_environ->isInputReady) {
        solcraft_environ->GLFW_invoke_CursorEnter(solcraft_environ->showingWindow, entered);
    }
}
*/

void critical_send_cursor_pos(jfloat x, jfloat y) {
#ifdef DEBUG
    LOGD("Sending cursor position \n");
#endif
    if (solcraft_environ->GLFW_invoke_CursorPos && solcraft_environ->isInputReady) {
#ifdef DEBUG
        LOGD("solcraft_environ->GLFW_invoke_CursorPos && solcraft_environ->isInputReady \n");
#endif
        if (!solcraft_environ->isCursorEntered) {
            if (solcraft_environ->GLFW_invoke_CursorEnter) {
                solcraft_environ->isCursorEntered = true;
                if (solcraft_environ->isUseStackQueueCall) {
                    sendData(EVENT_TYPE_CURSOR_ENTER, 1, 0, 0, 0);
                } else {
                    solcraft_environ->GLFW_invoke_CursorEnter((void*) solcraft_environ->showingWindow, 1);
                }
            } else if (solcraft_environ->isGrabbing) {
                // Some Minecraft versions does not use GLFWCursorEnterCallback
                // This is a smart check, as Minecraft will not in grab mode if already not.
                solcraft_environ->isCursorEntered = true;
            }
        }

        if (!solcraft_environ->isUseStackQueueCall) {
            solcraft_environ->GLFW_invoke_CursorPos((void*) solcraft_environ->showingWindow, (double) (x), (double) (y));
        } else {
            solcraft_environ->cursorX = x;
            solcraft_environ->cursorY = y;
        }
    }
}

void noncritical_send_cursor_pos(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz,  jfloat x, jfloat y) {
    critical_send_cursor_pos(x, y);
}
#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
void critical_send_key(jint key, jint scancode, jint action, jint mods) {
    if (solcraft_environ->GLFW_invoke_Key && solcraft_environ->isInputReady) {
        solcraft_environ->keyDownBuffer[max(0, key-31)] = (jbyte) action;
        if (solcraft_environ->isUseStackQueueCall) {
            sendData(EVENT_TYPE_KEY, key, scancode, action, mods);
        } else {
            solcraft_environ->GLFW_invoke_Key((void*) solcraft_environ->showingWindow, key, scancode, action, mods);
        }
    }
}
void noncritical_send_key(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jint key, jint scancode, jint action, jint mods) {
    critical_send_key(key, scancode, action, mods);
}

void critical_send_mouse_button(jint button, jint action, jint mods) {
    if (solcraft_environ->GLFW_invoke_MouseButton && solcraft_environ->isInputReady) {
        solcraft_environ->mouseDownBuffer[max(0, button)] = (jbyte) action;
        if (solcraft_environ->isUseStackQueueCall) {
            sendData(EVENT_TYPE_MOUSE_BUTTON, button, action, mods, 0);
        } else {
            solcraft_environ->GLFW_invoke_MouseButton((void*) solcraft_environ->showingWindow, button, action, mods);
        }
    }
}

void noncritical_send_mouse_button(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jint button, jint action, jint mods) {
    critical_send_mouse_button(button, action, mods);
}

void critical_send_screen_size(jint width, jint height) {
    solcraft_environ->savedWidth = width;
    solcraft_environ->savedHeight = height;
    if (solcraft_environ->isInputReady) {
        if (solcraft_environ->GLFW_invoke_FramebufferSize) {
            if (solcraft_environ->isUseStackQueueCall) {
                sendData(EVENT_TYPE_FRAMEBUFFER_SIZE, width, height, 0, 0);
            } else {
                solcraft_environ->GLFW_invoke_FramebufferSize((void*) solcraft_environ->showingWindow, width, height);
            }
        }

        if (solcraft_environ->GLFW_invoke_WindowSize) {
            if (solcraft_environ->isUseStackQueueCall) {
                sendData(EVENT_TYPE_WINDOW_SIZE, width, height, 0, 0);
            } else {
                solcraft_environ->GLFW_invoke_WindowSize((void*) solcraft_environ->showingWindow, width, height);
            }
        }
    }
}

void noncritical_send_screen_size(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jint width, jint height) {
    critical_send_screen_size(width, height);
}

void critical_send_scroll(jdouble xoffset, jdouble yoffset) {
    if (solcraft_environ->GLFW_invoke_Scroll && solcraft_environ->isInputReady) {
        if (solcraft_environ->isUseStackQueueCall) {
            sendData(EVENT_TYPE_SCROLL, (int)xoffset, (int)yoffset, 0, 0);
        } else {
            solcraft_environ->GLFW_invoke_Scroll((void*) solcraft_environ->showingWindow, (double) xoffset, (double) yoffset);
        }
    }
}

void noncritical_send_scroll(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jdouble xoffset, jdouble yoffset) {
    critical_send_scroll(xoffset, yoffset);
}


JNIEXPORT void JNICALL Java_org_lwjgl_glfw_GLFW_nglfwSetShowingWindow(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jlong window) {
    solcraft_environ->showingWindow = (long) window;
}

JNIEXPORT void JNICALL Java_org_lwjgl_glfw_CallbackBridge_nativeSetWindowAttrib(__attribute__((unused)) JNIEnv* env, __attribute__((unused)) jclass clazz, jint attrib, jint value) {
    if (!solcraft_environ->showingWindow || !solcraft_environ->isUseStackQueueCall) {
        // If the window is not shown, there is nothing to do yet.
        // For Minecraft < 1.13, calling to JNI functions here crashes the JVM for some reason, therefore it is skipped for now.
        return;
    }

    (*solcraft_environ->runtimeJNIEnvPtr_JRE)->CallStaticVoidMethod(
        solcraft_environ->runtimeJNIEnvPtr_JRE,
        solcraft_environ->vmGlfwClass, solcraft_environ->method_glftSetWindowAttrib,
        (jlong) solcraft_environ->showingWindow, attrib, value
    );
}
const static JNINativeMethod critical_fcns[] = {
        {"nativeSetUseInputStackQueue", "(Z)V", critical_set_stackqueue},
        {"nativeSendChar", "(C)Z", critical_send_char},
        {"nativeSendCharMods", "(CI)Z", critical_send_char_mods},
        {"nativeSendKey", "(IIII)V", critical_send_key},
        {"nativeSendCursorPos", "(FF)V", critical_send_cursor_pos},
        {"nativeSendMouseButton", "(III)V", critical_send_mouse_button},
        {"nativeSendScroll", "(DD)V", critical_send_scroll},
        {"nativeSendScreenSize", "(II)V", critical_send_screen_size}
};

const static JNINativeMethod noncritical_fcns[] = {
        {"nativeSetUseInputStackQueue", "(Z)V", noncritical_set_stackqueue},
        {"nativeSendChar", "(C)Z", noncritical_send_char},
        {"nativeSendCharMods", "(CI)Z", noncritical_send_char_mods},
        {"nativeSendKey", "(IIII)V", noncritical_send_key},
        {"nativeSendCursorPos", "(FF)V", noncritical_send_cursor_pos},
        {"nativeSendMouseButton", "(III)V", noncritical_send_mouse_button},
        {"nativeSendScroll", "(DD)V", noncritical_send_scroll},
        {"nativeSendScreenSize", "(II)V", noncritical_send_screen_size}
};


static bool criticalNativeAvailable;

void dvm_testCriticalNative(void* arg0, void* arg1, void* arg2, void* arg3) {
    if(arg0 != 0 && arg2 == 0 && arg3 == 0) {
        criticalNativeAvailable = false;
    }else if (arg0 == 0 && arg1 == 0){
        criticalNativeAvailable = true;
    }else {
        criticalNativeAvailable = false; // just to be safe
    }
}

static bool tryCriticalNative(JNIEnv *env) {
    static const JNINativeMethod testJNIMethod[] = {
            { "testCriticalNative", "(II)V", dvm_testCriticalNative}
    };
    jclass criticalNativeTest = (*env)->FindClass(env, "net/kdt/pojavlaunch/CriticalNativeTest");
    if(criticalNativeTest == NULL) {
        (*env)->ExceptionClear(env);
        return false;
    }
    jmethodID criticalNativeTestMethod = (*env)->GetStaticMethodID(env, criticalNativeTest, "invokeTest", "()V");
    (*env)->RegisterNatives(env, criticalNativeTest, testJNIMethod, 1);
    (*env)->CallStaticVoidMethod(env, criticalNativeTest, criticalNativeTestMethod);
    (*env)->UnregisterNatives(env, criticalNativeTest);
    return criticalNativeAvailable;
}

static void registerFunctions(JNIEnv *env) {
    bool use_critical_cc = tryCriticalNative(env);
    jclass bridge_class = (*env)->FindClass(env, "org/lwjgl/glfw/CallbackBridge");
    if(use_critical_cc) {
        __android_log_print(ANDROID_LOG_INFO, "pojavexec", "CriticalNative is available. Enjoy the 4.6x times faster input!");
    }else{
        __android_log_print(ANDROID_LOG_INFO, "pojavexec", "CriticalNative is not available. Upgrade, maybe?");
    }
    (*env)->RegisterNatives(env,
                            bridge_class,
                            use_critical_cc ? critical_fcns : noncritical_fcns,
                            sizeof(critical_fcns)/sizeof(critical_fcns[0]));
}
