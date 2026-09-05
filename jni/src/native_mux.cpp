#include "util.hpp"
#include <jni.h>
#include <juice/juice.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct raw_mux {
    JavaVM* vm;
    jobject listener;
    jmethodID dispatch;
    char* address;
    int port;
};

static bool raw_packet(const void* data, size_t size, const char* address, uint16_t port, void* ptr) {
    const auto mux = static_cast<raw_mux*>(ptr);
    if (size > 65535) {
        return false;
    }
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint state = mux->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (mux->vm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) != JNI_OK) {
            return false;
        }
        attached = true;
    } else if (state != JNI_OK) {
        return false;
    }
    bool accepted = false;
    if (env->PushLocalFrame(4) == 0) {
        jbyteArray packet = env->NewByteArray(static_cast<jsize>(size));
        if (packet != nullptr) {
            env->SetByteArrayRegion(packet, 0, static_cast<jsize>(size), static_cast<const jbyte*>(data));
            jstring host = env->ExceptionCheck() ? nullptr : env->NewStringUTF(address);
            if (host != nullptr) {
                accepted = env->CallBooleanMethod(mux->listener, mux->dispatch, packet, host, static_cast<jint>(port));
            }
        }
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            accepted = false;
        }
        env->PopLocalFrame(nullptr);
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (attached) {
        mux->vm->DetachCurrentThread();
    }
    return accepted;
}

extern "C" JNIEXPORT jlong JNICALL Java_tel_schich_libdatachannel_RawUdpMuxListener_openNative(
        JNIEnv* env, jobject self, jstring address, const jint port) {
    const auto mux = static_cast<raw_mux*>(calloc(1, sizeof(raw_mux)));
    if (mux == nullptr) {
        return 0;
    }
    const char* host = env->GetStringUTFChars(address, nullptr);
    if (host == nullptr) {
        free(mux);
        return 0;
    }
    mux->address = strdup(host);
    env->ReleaseStringUTFChars(address, host);
    mux->port = port;
    env->GetJavaVM(&mux->vm);
    mux->listener = env->NewGlobalRef(self);
    jclass clazz = env->GetObjectClass(self);
    mux->dispatch = clazz != nullptr ? env->GetMethodID(clazz, "dispatch", "([BLjava/lang/String;I)Z") : nullptr;
    if (clazz != nullptr) {
        env->DeleteLocalRef(clazz);
    }
    if (mux->address == nullptr || mux->listener == nullptr || mux->dispatch == nullptr || env->ExceptionCheck() ||
        juice_mux_listen_raw(mux->address, port, raw_packet, mux) != 0) {
        if (mux->listener != nullptr) {
            env->DeleteGlobalRef(mux->listener);
        }
        free(mux->address);
        free(mux);
        return 0;
    }
    return static_cast<jlong>(reinterpret_cast<intptr_t>(mux));
}

extern "C" JNIEXPORT void JNICALL Java_tel_schich_libdatachannel_RawUdpMuxListener_closeNative(
        JNIEnv* env, jclass clazz, const jlong handle) {
    const auto mux = reinterpret_cast<raw_mux*>(static_cast<intptr_t>(handle));
    // Registry locking waits for an in-flight callback before releasing JNI refs.
    if (juice_mux_listen_raw(mux->address, mux->port, nullptr, nullptr) != 0) {
        throw_native_exception(env, "Failed to close raw UDP mux");
        return;
    }
    env->DeleteGlobalRef(mux->listener);
    free(mux->address);
    free(mux);
}

extern "C" JNIEXPORT jlongArray JNICALL Java_tel_schich_libdatachannel_RawUdpMuxListener_statsNative(
        JNIEnv* env, jclass clazz, const jlong handle) {
    const auto mux = reinterpret_cast<raw_mux*>(static_cast<intptr_t>(handle));
    juice_mux_stats_t stats;
    if (juice_mux_get_stats(mux->address, mux->port, &stats) != 0) {
        throw_native_exception(env, "Raw UDP mux statistics unavailable");
        return nullptr;
    }
    const jlong values[] = {
            static_cast<jlong>(stats.received),
            static_cast<jlong>(stats.rejected),
            static_cast<jlong>(stats.agents),
            static_cast<jlong>(stats.mapped_tuples),
    };
    jlongArray result = env->NewLongArray(4);
    if (result != nullptr) {
        env->SetLongArrayRegion(result, 0, 4, values);
    }
    return result;
}

extern "C" JNIEXPORT void JNICALL Java_tel_schich_libdatachannel_RawUdpMuxListener_replayNative(
        JNIEnv* env, jclass clazz, const jlong handle, jbyteArray packet, jstring source_address,
        const jint source_port) {
    const auto mux = reinterpret_cast<raw_mux*>(static_cast<intptr_t>(handle));
    const jsize size = env->GetArrayLength(packet);
    if (size < 20 || size > 2048) {
        throw_native_exception(env, "Invalid deferred STUN size");
        return;
    }
    unsigned char data[2048];
    env->GetByteArrayRegion(packet, 0, size, reinterpret_cast<jbyte*>(data));
    if (env->ExceptionCheck()) {
        return;
    }
    const char* source = env->GetStringUTFChars(source_address, nullptr);
    if (source == nullptr) {
        return;
    }
    const int result = juice_mux_replay(mux->address, mux->port, source, source_port, data, static_cast<size_t>(size));
    env->ReleaseStringUTFChars(source_address, source);
    if (result != 0) {
        throw_native_exception(env, "Cannot queue deferred STUN request");
    }
}
