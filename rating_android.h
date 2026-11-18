// Google App Rating & In-App Review JNI bridge for Android.
// Connects native Velo C game thread to io.velo.rating.RatingBridge.
// Includes a pure JNI fallback for opening the store even if RatingBridge is absent.

#pragma once

#if defined(__ANDROID__)
#include <jni.h>
#include <android/native_activity.h>
#include <android/log.h>
#include <string.h>

#define VELO_RATING_TAG "VeloRatingJNI"

static inline JNIEnv* velo_rating_env(const ANativeActivity* act) {
	if (act == NULL || act->vm == NULL) return NULL;
	JavaVM* vm = act->vm;
	JNIEnv* env = NULL;
	if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
		if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return NULL;
	}
	return env;
}

static inline jclass velo_rating_class(JNIEnv* env, const ANativeActivity* act) {
	static jclass cached = NULL;
	if (cached != NULL) return cached;
	jclass act_cls = (*env)->GetObjectClass(env, act->clazz);
	jmethodID get_loader = (*env)->GetMethodID(env, act_cls, "getClassLoader", "()Ljava/lang/ClassLoader;");
	jobject loader = (*env)->CallObjectMethod(env, act->clazz, get_loader);
	jclass loader_cls = (*env)->GetObjectClass(env, loader);
	jmethodID load_class = (*env)->GetMethodID(env, loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
	jstring name = (*env)->NewStringUTF(env, "io.velo.rating.RatingBridge");
	jclass cls = (jclass)(*env)->CallObjectMethod(env, loader, load_class, name);
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		cls = NULL;
	}
	if (cls != NULL) cached = (jclass)(*env)->NewGlobalRef(env, cls);
	(*env)->DeleteLocalRef(env, name);
	(*env)->DeleteLocalRef(env, loader_cls);
	(*env)->DeleteLocalRef(env, loader);
	(*env)->DeleteLocalRef(env, act_cls);
	return cached;
}

static inline void velo_rating_android_init(void* activity) {
	const ANativeActivity* act = (const ANativeActivity*)activity;
	JNIEnv* env = velo_rating_env(act);
	if (!env) return;
	jclass cls = velo_rating_class(env, act);
	if (!cls) return;
	jmethodID m = (*env)->GetStaticMethodID(env, cls, "init", "(Landroid/app/Activity;)V");
	if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return; }
	(*env)->CallStaticVoidMethod(env, cls, m, act->clazz);
	if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

static inline int velo_rating_android_is_available(void* activity) {
	const ANativeActivity* act = (const ANativeActivity*)activity;
	JNIEnv* env = velo_rating_env(act);
	if (!env) return 0;
	jclass cls = velo_rating_class(env, act);
	if (!cls) return 0;
	jmethodID m = (*env)->GetStaticMethodID(env, cls, "isReviewAvailable", "(Landroid/app/Activity;)Z");
	if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
	jboolean r = (*env)->CallStaticBooleanMethod(env, cls, m, act->clazz);
	if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
	return r ? 1 : 0;
}

static inline int velo_rating_android_open_store(void* activity, const char* pkg) {
	const ANativeActivity* act = (const ANativeActivity*)activity;
	JNIEnv* env = velo_rating_env(act);
	if (!env) return 0;

	jclass cls = velo_rating_class(env, act);
	if (cls) {
		jmethodID m = (*env)->GetStaticMethodID(env, cls, "openStore", "(Landroid/app/Activity;Ljava/lang/String;)V");
		if (!(*env)->ExceptionCheck(env) && m) {
			jstring jpkg = (*env)->NewStringUTF(env, pkg ? pkg : "");
			(*env)->CallStaticVoidMethod(env, cls, m, act->clazz, jpkg);
			(*env)->DeleteLocalRef(env, jpkg);
			if (!(*env)->ExceptionCheck(env)) return 1;
			(*env)->ExceptionClear(env);
		} else {
			(*env)->ExceptionClear(env);
		}
	}

	// Fallback via pure JNI Intent
	jclass uri_cls = (*env)->FindClass(env, "android/net/Uri");
	jclass intent_cls = (*env)->FindClass(env, "android/content/Intent");
	if (!uri_cls || !intent_cls) {
		if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
		return 0;
	}

	// Package name fallback to activity.getPackageName()
	const char* target_pkg = pkg;
	char pkg_buf[256] = {0};
	if (!target_pkg || strlen(target_pkg) == 0) {
		jclass act_cls = (*env)->GetObjectClass(env, act->clazz);
		jmethodID get_pkg_m = (*env)->GetMethodID(env, act_cls, "getPackageName", "()Ljava/lang/String;");
		jstring act_pkg = (jstring)(*env)->CallObjectMethod(env, act->clazz, get_pkg_m);
		if (act_pkg && !(*env)->ExceptionCheck(env)) {
			const char* chars = (*env)->GetStringUTFChars(env, act_pkg, NULL);
			if (chars) {
				strncpy(pkg_buf, chars, sizeof(pkg_buf) - 1);
				(*env)->ReleaseStringUTFChars(env, act_pkg, chars);
				target_pkg = pkg_buf;
			}
			(*env)->DeleteLocalRef(env, act_pkg);
		} else {
			if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
		}
		(*env)->DeleteLocalRef(env, act_cls);
	}

	char market_uri_str[512];
	snprintf(market_uri_str, sizeof(market_uri_str), "market://details?id=%s", target_pkg ? target_pkg : "");

	jmethodID uri_parse = (*env)->GetStaticMethodID(env, uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
	jstring j_market_uri = (*env)->NewStringUTF(env, market_uri_str);
	jobject uri_obj = (*env)->CallStaticObjectMethod(env, uri_cls, uri_parse, j_market_uri);
	(*env)->DeleteLocalRef(env, j_market_uri);

	jfieldID action_view_field = (*env)->GetStaticFieldID(env, intent_cls, "ACTION_VIEW", "Ljava/lang/String;");
	jstring action_view = (jstring)(*env)->GetStaticObjectField(env, intent_cls, action_view_field);

	jmethodID intent_init = (*env)->GetMethodID(env, intent_cls, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");
	jobject intent_obj = (*env)->NewObject(env, intent_cls, intent_init, action_view, uri_obj);
	(*env)->DeleteLocalRef(env, uri_obj);
	(*env)->DeleteLocalRef(env, action_view);

	// FLAG_ACTIVITY_NEW_TASK = 0x10000000
	jmethodID add_flags_m = (*env)->GetMethodID(env, intent_cls, "addFlags", "(I)Landroid/content/Intent;");
	(*env)->CallObjectMethod(env, intent_obj, add_flags_m, (jint)0x10000000);

	jclass act_cls = (*env)->GetObjectClass(env, act->clazz);
	jmethodID start_act_m = (*env)->GetMethodID(env, act_cls, "startActivity", "(Landroid/content/Intent;)V");
	(*env)->CallVoidMethod(env, act->clazz, start_act_m, intent_obj);

	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		// ActivityNotFoundException: fallback to https://play.google.com/store/apps/details?id=...
		char web_uri_str[512];
		snprintf(web_uri_str, sizeof(web_uri_str), "https://play.google.com/store/apps/details?id=%s", target_pkg ? target_pkg : "");
		jstring j_web_uri = (*env)->NewStringUTF(env, web_uri_str);
		jobject web_uri_obj = (*env)->CallStaticObjectMethod(env, uri_cls, uri_parse, j_web_uri);
		(*env)->DeleteLocalRef(env, j_web_uri);

		jstring action_view2 = (jstring)(*env)->GetStaticObjectField(env, intent_cls, action_view_field);
		jobject web_intent = (*env)->NewObject(env, intent_cls, intent_init, action_view2, web_uri_obj);
		(*env)->DeleteLocalRef(env, web_uri_obj);
		(*env)->DeleteLocalRef(env, action_view2);

		(*env)->CallObjectMethod(env, web_intent, add_flags_m, (jint)0x10000000);
		(*env)->CallVoidMethod(env, act->clazz, start_act_m, web_intent);
		if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
		(*env)->DeleteLocalRef(env, web_intent);
	}

	(*env)->DeleteLocalRef(env, intent_obj);
	(*env)->DeleteLocalRef(env, act_cls);
	(*env)->DeleteLocalRef(env, intent_cls);
	(*env)->DeleteLocalRef(env, uri_cls);
	return 1;
}

static inline int velo_rating_android_request_review(void* activity, const char* fallback_pkg, int fallback_to_store) {
	const ANativeActivity* act = (const ANativeActivity*)activity;
	JNIEnv* env = velo_rating_env(act);
	if (!env) return 0;
	jclass cls = velo_rating_class(env, act);
	if (cls) {
		jmethodID m = (*env)->GetStaticMethodID(env, cls, "requestReview", "(Landroid/app/Activity;Ljava/lang/String;Z)V");
		if (!(*env)->ExceptionCheck(env) && m) {
			jstring jpkg = (*env)->NewStringUTF(env, fallback_pkg ? fallback_pkg : "");
			(*env)->CallStaticVoidMethod(env, cls, m, act->clazz, jpkg, (jboolean)(fallback_to_store ? JNI_TRUE : JNI_FALSE));
			(*env)->DeleteLocalRef(env, jpkg);
			if (!(*env)->ExceptionCheck(env)) return 1;
			(*env)->ExceptionClear(env);
		} else {
			(*env)->ExceptionClear(env);
		}
	}

	if (fallback_to_store) {
		return velo_rating_android_open_store(activity, fallback_pkg);
	}
	return 0;
}

static inline int velo_rating_android_poll_result(void* activity) {
	const ANativeActivity* act = (const ANativeActivity*)activity;
	JNIEnv* env = velo_rating_env(act);
	if (!env) return 0;
	jclass cls = velo_rating_class(env, act);
	if (!cls) return 0;
	jmethodID m = (*env)->GetStaticMethodID(env, cls, "pollReviewResult", "()I");
	if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
	jint r = (*env)->CallStaticIntMethod(env, cls, m);
	if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
	return (int)r;
}

#else

static inline void velo_rating_android_init(void* activity) {}
static inline int velo_rating_android_is_available(void* activity) { return 0; }
static inline int velo_rating_android_open_store(void* activity, const char* pkg) { return 0; }
static inline int velo_rating_android_request_review(void* activity, const char* fallback_pkg, int fallback_to_store) { return 0; }
static inline int velo_rating_android_poll_result(void* activity) { return 0; }

#endif // __ANDROID__
