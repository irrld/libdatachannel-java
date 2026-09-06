#include "util.hpp"
#include <jni.h>
#include <rtc/rtc.h>
#include <stdint.h>
#include <stdlib.h>

struct ice_mux {
    int listener;
    jobject owner;
    jmethodID dispatch;
};

extern "C" JNIEXPORT jint JNICALL Java_tel_schich_libdatachannel_IceUdpMuxListener_listenerIdNative(
        JNIEnv* env, jclass clazz, const jlong handle) {
    return reinterpret_cast<ice_mux*>(static_cast<intptr_t>(handle))->listener;
}

static void RTC_API incoming_request(int listener, const rtcIceUdpMuxRequest* request, void* ptr) {
    const auto mux = static_cast<ice_mux*>(ptr);
    JNIEnv* env = get_jni_env();
    bool queued = false;
    if (env != nullptr && env->PushLocalFrame(4) == 0) {
        jstring local = env->NewStringUTF(request->localUfrag);
        jstring remote = !env->ExceptionCheck() ? env->NewStringUTF(request->remoteUfrag) : nullptr;
        jstring address = !env->ExceptionCheck() ? env->NewStringUTF(request->remoteAddress) : nullptr;
        if (!env->ExceptionCheck()) {
            queued = env->CallBooleanMethod(mux->owner, mux->dispatch, static_cast<jlong>(request->id), local, remote,
                                            address, static_cast<jint>(request->remotePort));
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            queued = false;
        }
        env->PopLocalFrame(nullptr);
    } else if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (!queued) {
        rtcRejectIceUdpMuxRequest(listener, request->id);
    }
}

extern "C" JNIEXPORT jlong JNICALL Java_tel_schich_libdatachannel_IceUdpMuxListener_openNative(
        JNIEnv* env, jobject self, jstring address, const jint port, const jint maxPending, const jint timeoutMs) {
    const auto mux = static_cast<ice_mux*>(calloc(1, sizeof(ice_mux)));
    if (mux == nullptr) {
        return 0;
    }
    mux->listener = -1;
    mux->owner = env->NewGlobalRef(self);
    jclass clazz = !env->ExceptionCheck() ? env->GetObjectClass(self) : nullptr;
    mux->dispatch = clazz != nullptr
            ? env->GetMethodID(clazz, "dispatch", "(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Z")
            : nullptr;
    if (clazz != nullptr) {
        env->DeleteLocalRef(clazz);
    }
    const char* host = !env->ExceptionCheck() ? env->GetStringUTFChars(address, nullptr) : nullptr;
    if (host != nullptr && mux->owner != nullptr && mux->dispatch != nullptr) {
        rtcIceUdpMuxListenerConfiguration config = {};
        config.bindAddress = host;
        config.port = static_cast<uint16_t>(port);
        config.maxPendingRequests = static_cast<unsigned int>(maxPending);
        config.requestTimeoutMs = static_cast<unsigned int>(timeoutMs);
        mux->listener = rtcCreateIceUdpMuxListener(&config, incoming_request, mux);
    }
    if (host != nullptr) {
        env->ReleaseStringUTFChars(address, host);
    }
    if (mux->listener < 0) {
        if (mux->owner != nullptr) {
            env->DeleteGlobalRef(mux->owner);
        }
        free(mux);
        return 0;
    }
    return static_cast<jlong>(reinterpret_cast<intptr_t>(mux));
}

extern "C" JNIEXPORT void JNICALL Java_tel_schich_libdatachannel_IceUdpMuxListener_closeNative(
        JNIEnv* env, jclass clazz, const jlong handle) {
    const auto mux = reinterpret_cast<ice_mux*>(static_cast<intptr_t>(handle));
    if (rtcDeleteIceUdpMuxListener(mux->listener) != RTC_ERR_SUCCESS) {
        throw_native_exception(env, "Failed to close ICE UDP mux listener");
        return;
    }
    // Native deletion waits for in-flight metadata callbacks before releasing this reference.
    env->DeleteGlobalRef(mux->owner);
    free(mux);
}

extern "C" JNIEXPORT jint JNICALL Java_tel_schich_libdatachannel_IceUdpMuxListener_acceptNative(
        JNIEnv* env, jclass clazz, const jint listener, const jlong requestId, const jint peer) {
    return rtcAcceptIceUdpMuxPeer(listener, static_cast<uint64_t>(requestId), peer);
}

extern "C" JNIEXPORT jint JNICALL Java_tel_schich_libdatachannel_IceUdpMuxListener_rejectNative(
        JNIEnv* env, jclass clazz, const jint listener, const jlong requestId) {
    return rtcRejectIceUdpMuxRequest(listener, static_cast<uint64_t>(requestId));
}

extern "C" JNIEXPORT jlongArray JNICALL Java_tel_schich_libdatachannel_IceUdpMuxListener_statsNative(
        JNIEnv* env, jclass clazz, const jint listener) {
    rtcIceUdpMuxListenerStats stats;
    if (rtcGetIceUdpMuxListenerStats(listener, &stats) != RTC_ERR_SUCCESS) {
        throw_native_exception(env, "ICE UDP mux statistics unavailable");
        return nullptr;
    }
    const jlong values[] = {
            static_cast<jlong>(stats.received),
            static_cast<jlong>(stats.rejected),
            static_cast<jlong>(stats.agents),
            static_cast<jlong>(stats.mappedTuples),
            static_cast<jlong>(stats.pendingRequests),
            static_cast<jlong>(stats.notifications),
            static_cast<jlong>(stats.duplicates),
    };
    jlongArray result = env->NewLongArray(7);
    if (result != nullptr) {
        env->SetLongArrayRegion(result, 0, 7, values);
    }
    return result;
}
