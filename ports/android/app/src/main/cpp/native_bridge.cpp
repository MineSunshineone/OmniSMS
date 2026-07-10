#include <jni.h>

#include <cstdint>
#include <limits>
#include <mutex>
#include <string>

#include "omnisms/admin.h"
#include "omnisms/config.h"
#include "omnisms/email.h"
#include "omnisms/inbox.h"
#include "omnisms/phone.h"
#include "omnisms/push.h"
#include "omnisms/rules.h"
#include "omnisms/scheduler.h"
#include "omnisms/text.h"

namespace {

std::mutex inbox_mutex;
omnisms::MessageStore inbox_store;

std::string from_jstring(JNIEnv* env, jstring value)
{
    if (!value) return {};
    jclass string_class = env->FindClass("java/lang/String");
    jmethodID get_bytes = env->GetMethodID(string_class, "getBytes", "(Ljava/lang/String;)[B");
    jstring charset = env->NewStringUTF("UTF-8");
    auto bytes = static_cast<jbyteArray>(env->CallObjectMethod(value, get_bytes, charset));
    const jsize size = bytes ? env->GetArrayLength(bytes) : 0;
    std::string result(static_cast<size_t>(size), '\0');
    if (size > 0) env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(result.data()));
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(string_class);
    return result;
}

jstring to_jstring(JNIEnv* env, const std::string& value)
{
    jclass string_class = env->FindClass("java/lang/String");
    jmethodID constructor = env->GetMethodID(string_class, "<init>", "([BLjava/lang/String;)V");
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(value.size()));
    if (!value.empty()) {
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(value.size()),
                                reinterpret_cast<const jbyte*>(value.data()));
    }
    jstring charset = env->NewStringUTF("UTF-8");
    auto result = static_cast<jstring>(env->NewObject(string_class, constructor, bytes, charset));
    env->DeleteLocalRef(charset);
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(string_class);
    return result;
}

void bool_prop(std::string& out, const char* name, bool value)
{
    out += '"'; out += name; out += "\":"; out += value ? "true" : "false";
}

std::string error_json(const std::string& message)
{
    std::string out = "{\"accepted\":false,\"reason\":\"config\",";
    omnisms::json_prop(out, "message", message);
    out += '}';
    return out;
}

void email_plan_prop(std::string& out, const omnisms::EmailConfig& config,
                     bool selected, const omnisms::EmailContent& content)
{
    const bool requested = selected && omnisms::email_configured(config);
    bool_prop(out, "emailRequested", requested);
    out += ",\"email\":";
    if (!requested) {
        out += "null";
        return;
    }
    out += '{';
    omnisms::json_prop(out, "subject", content.subject); out += ',';
    omnisms::json_prop(out, "body", content.body);
    out += '}';
}

}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_omnisms_app_CoreBridge_nativeInboxClear(JNIEnv*, jobject)
{
    std::lock_guard<std::mutex> lock(inbox_mutex);
    inbox_store.clear();
}

extern "C" JNIEXPORT void JNICALL
Java_com_omnisms_app_CoreBridge_nativeInboxRestoreReceived(
    JNIEnv* env, jobject, jlong id, jlong epoch, jstring sender, jstring timestamp,
    jstring text, jboolean forwarded)
{
    omnisms::InboxEntry entry;
    entry.id = static_cast<uint32_t>(id);
    entry.recvEpoch = static_cast<uint32_t>(epoch);
    entry.sender = from_jstring(env, sender);
    entry.ts = from_jstring(env, timestamp);
    entry.text = from_jstring(env, text);
    entry.forwarded = forwarded == JNI_TRUE;
    std::lock_guard<std::mutex> lock(inbox_mutex);
    inbox_store.restore_received(entry);
}

extern "C" JNIEXPORT void JNICALL
Java_com_omnisms_app_CoreBridge_nativeInboxRestoreSent(
    JNIEnv* env, jobject, jlong id, jlong epoch, jstring target, jstring text, jboolean ok)
{
    omnisms::SentEntry entry;
    entry.id = static_cast<uint32_t>(id);
    entry.sentEpoch = static_cast<uint32_t>(epoch);
    entry.target = from_jstring(env, target);
    entry.text = from_jstring(env, text);
    entry.ok = ok == JNI_TRUE;
    std::lock_guard<std::mutex> lock(inbox_mutex);
    inbox_store.restore_sent(entry);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_omnisms_app_CoreBridge_nativeInboxAddReceived(
    JNIEnv* env, jobject, jstring sender, jstring text, jstring timestamp, jlong epoch)
{
    std::lock_guard<std::mutex> lock(inbox_mutex);
    return static_cast<jlong>(inbox_store.add_received(
        from_jstring(env, sender), from_jstring(env, text), from_jstring(env, timestamp),
        static_cast<uint32_t>(epoch)).id);
}

extern "C" JNIEXPORT void JNICALL
Java_com_omnisms_app_CoreBridge_nativeInboxAddSent(
    JNIEnv* env, jobject, jstring target, jstring text, jboolean ok, jlong epoch)
{
    std::lock_guard<std::mutex> lock(inbox_mutex);
    inbox_store.add_sent(from_jstring(env, target), from_jstring(env, text), ok == JNI_TRUE,
                         static_cast<uint32_t>(epoch));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_omnisms_app_CoreBridge_nativeInboxSetForwarded(
    JNIEnv*, jobject, jlong id, jboolean forwarded)
{
    std::lock_guard<std::mutex> lock(inbox_mutex);
    return inbox_store.set_forwarded(static_cast<uint32_t>(id), forwarded == JNI_TRUE)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_omnisms_app_CoreBridge_nativeInboxDelete(JNIEnv*, jobject, jlong id)
{
    std::lock_guard<std::mutex> lock(inbox_mutex);
    return inbox_store.delete_received(static_cast<uint32_t>(id)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_omnisms_app_CoreBridge_nativeInboxJson(
    JNIEnv* env, jobject, jboolean sent_box, jint limit)
{
    std::lock_guard<std::mutex> lock(inbox_mutex);
    return to_jstring(env, inbox_store.json(sent_box == JNI_TRUE, static_cast<int>(limit)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_omnisms_app_CoreBridge_nativeValidateConfig(JNIEnv* env, jobject, jstring json)
{
    omnisms::AppConfig config;
    std::string error;
    if (!omnisms::parse_config_json(from_jstring(env, json), config, &error)) {
        return to_jstring(env, error);
    }
    if (!omnisms::validate_forward_rules(config.forwardRules, &error)) {
        return to_jstring(env, error);
    }
    return to_jstring(env, "");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_omnisms_app_CoreBridge_nativeCanonicalPhone(JNIEnv* env, jobject, jstring phone)
{
    return to_jstring(env, omnisms::canonical_phone(from_jstring(env, phone)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_omnisms_app_CoreBridge_nativeEvaluateAdmin(
    JNIEnv* env, jobject, jstring config_json, jstring sender, jstring text,
    jlong uptime_seconds, jboolean sms_busy, jboolean reset_pending)
{
    omnisms::AppConfig config;
    std::string error;
    if (!omnisms::parse_config_json(from_jstring(env, config_json), config, &error)) {
        std::string out = "{\"handled\":false,";
        omnisms::json_prop(out, "error", error);
        out += '}';
        return to_jstring(env, out);
    }
    const omnisms::AdminCommandDecision decision = omnisms::evaluate_admin_command(
        config.adminPhone, from_jstring(env, sender), from_jstring(env, text),
        static_cast<int64_t>(uptime_seconds), sms_busy == JNI_TRUE, reset_pending == JNI_TRUE);
    std::string out = std::string("{\"handled\":") + (decision.handled() ? "true," : "false,");
    out += "\"status\":" + std::to_string(static_cast<int>(decision.status)) + ',';
    const char* action = "reject";
    if (decision.status == omnisms::AdminCommandStatus::SendSms) action = "send_sms";
    else if (decision.status == omnisms::AdminCommandStatus::Reset) action = "reset";
    omnisms::json_prop(out, "action", action); out += ',';
    omnisms::json_prop(out, "command", decision.command); out += ',';
    omnisms::json_prop(out, "target", decision.target); out += ',';
    omnisms::json_prop(out, "content", decision.content); out += ',';
    omnisms::json_prop(out, "message", omnisms::admin_command_message(decision.status));
    out += '}';
    return to_jstring(env, out);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_omnisms_app_CoreBridge_nativeEvaluatePeriodic(
    JNIEnv*, jobject, jboolean enabled, jlong last_epoch, jlong now_epoch, jint interval_days)
{
    return static_cast<jint>(omnisms::evaluate_periodic(
        enabled == JNI_TRUE, static_cast<uint32_t>(last_epoch),
        static_cast<uint32_t>(now_epoch), static_cast<int>(interval_days)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_omnisms_app_CoreBridge_nativeFailureRetryBaseline(
    JNIEnv*, jobject, jlong now_epoch, jint interval_days)
{
    return static_cast<jlong>(omnisms::failure_retry_baseline(
        static_cast<uint32_t>(now_epoch), static_cast<int>(interval_days)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_omnisms_app_CoreBridge_nativeDailyDueDay(
    JNIEnv*, jobject, jboolean enabled, jint target_hour, jlong last_day,
    jlong now_epoch, jint timezone_offset_min)
{
    const uint32_t now = static_cast<uint32_t>(now_epoch);
    if (!omnisms::daily_due(enabled == JNI_TRUE, static_cast<int>(target_hour),
                            static_cast<int64_t>(last_day), now,
                            static_cast<int>(timezone_offset_min))) {
        return std::numeric_limits<jlong>::min();
    }
    return static_cast<jlong>(omnisms::local_day_hour(
        now, static_cast<int>(timezone_offset_min)).day);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_omnisms_app_CoreBridge_nativeDailyRebootDueDay(
    JNIEnv*, jobject, jboolean enabled, jint target_hour, jlong last_day,
    jlong now_epoch, jint timezone_offset_min, jlong uptime_seconds, jboolean system_idle)
{
    const uint32_t now = static_cast<uint32_t>(now_epoch);
    if (!omnisms::daily_reboot_due(
            enabled == JNI_TRUE, static_cast<int>(target_hour), static_cast<int64_t>(last_day),
            now, static_cast<int>(timezone_offset_min), static_cast<int64_t>(uptime_seconds),
            system_idle == JNI_TRUE)) {
        return std::numeric_limits<jlong>::min();
    }
    return static_cast<jlong>(omnisms::local_day_hour(
        now, static_cast<int>(timezone_offset_min)).day);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_omnisms_app_CoreBridge_nativePlanNotification(
    JNIEnv* env, jobject, jstring config_json, jstring title, jstring body,
    jstring timestamp, jstring receiver, jlong now_epoch_ms)
{
    omnisms::AppConfig config;
    std::string error;
    if (!omnisms::parse_config_json(from_jstring(env, config_json), config, &error)) {
        return to_jstring(env, error_json(error));
    }
    omnisms::SmsEvent event{
        from_jstring(env, title), from_jstring(env, body),
        from_jstring(env, timestamp), from_jstring(env, receiver)};
    std::string out = "{\"accepted\":true,";
    email_plan_prop(out, config.email, true,
                    omnisms::build_notification_email(event.sender, event.text));
    out += ',';
    out += "\"requests\":[";
    bool first = true;
    if (config.pushEnabled) {
        for (const omnisms::PushChannel& channel : config.pushChannels) {
            omnisms::HttpRequest request;
            if (!omnisms::build_push_request(channel, event, true,
                                              static_cast<int64_t>(now_epoch_ms), request)) continue;
            if (!first) out += ',';
            first = false;
            out += "{";
            omnisms::json_prop(out, "method", request.method); out += ',';
            omnisms::json_prop(out, "url", request.url); out += ',';
            omnisms::json_prop(out, "contentType", request.content_type); out += ',';
            omnisms::json_prop(out, "body", request.body); out += ',';
            out += "\"timeoutMs\":" + std::to_string(request.timeout_ms) + '}';
        }
    }
    out += "]}";
    return to_jstring(env, out);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_omnisms_app_CoreBridge_nativePlanSms(
    JNIEnv* env, jobject, jstring config_json, jstring sender, jstring text,
    jstring timestamp, jstring receiver, jlong now_epoch_ms)
{
    omnisms::AppConfig config;
    std::string error;
    if (!omnisms::parse_config_json(from_jstring(env, config_json), config, &error)) {
        return to_jstring(env, error_json(error));
    }
    omnisms::SmsEvent event{
        from_jstring(env, sender), from_jstring(env, text),
        from_jstring(env, timestamp), from_jstring(env, receiver)};
    if (omnisms::number_blacklisted(config.numberBlacklist, event.sender)) {
        return to_jstring(env, "{\"accepted\":false,\"reason\":\"blacklist\",\"requests\":[]}");
    }
    const omnisms::ForwardDecision decision =
        omnisms::eval_forward_rules(config.forwardRules, event.sender, event.text);
    if (decision.matched && decision.drop) {
        return to_jstring(env, "{\"accepted\":false,\"reason\":\"rule_drop\",\"requests\":[]}");
    }

    const uint32_t mask = decision.matched ? decision.chMask : 0xFFFFFFFFu;
    std::string out = "{\"accepted\":true,";
    bool_prop(out, "matchedRule", decision.matched); out += ',';
    email_plan_prop(out, config.email, decision.matched ? decision.email : true,
                    omnisms::build_sms_email(event));
    out += ',';
    out += "\"requests\":[";
    bool first = true;
    if (config.pushEnabled) {
        for (size_t i = 0; i < config.pushChannels.size(); ++i) {
            if (!(mask & (1u << i))) continue;
            omnisms::HttpRequest request;
            if (!omnisms::build_push_request(config.pushChannels[i], event, false,
                                              static_cast<int64_t>(now_epoch_ms), request)) continue;
            if (!first) out += ',';
            first = false;
            out += "{";
            omnisms::json_prop(out, "method", request.method); out += ',';
            omnisms::json_prop(out, "url", request.url); out += ',';
            omnisms::json_prop(out, "contentType", request.content_type); out += ',';
            omnisms::json_prop(out, "body", request.body); out += ',';
            out += "\"timeoutMs\":" + std::to_string(request.timeout_ms) + '}';
        }
    }
    out += "]}";
    return to_jstring(env, out);
}
