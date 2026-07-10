package com.omnisms.app

import android.content.Context
import org.json.JSONArray

object InboxStore {
    private const val PREFS = "omnisms_inbox"
    private const val KEY_RECEIVED = "received_v1"
    private const val KEY_SENT = "sent_v1"
    private var loaded = false

    @Synchronized
    private fun ensureLoaded(context: Context) {
        if (loaded) return
        CoreBridge.nativeInboxClear()
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val received = runCatching { JSONArray(prefs.getString(KEY_RECEIVED, "[]")) }
            .getOrDefault(JSONArray())
        for (index in received.length() - 1 downTo 0) {
            val item = received.optJSONObject(index) ?: continue
            CoreBridge.nativeInboxRestoreReceived(
                item.optLong("id"), item.optLong("recv"), item.optString("sender"),
                item.optString("ts"), item.optString("text"), item.optBoolean("fwd"),
            )
        }
        val sent = runCatching { JSONArray(prefs.getString(KEY_SENT, "[]")) }
            .getOrDefault(JSONArray())
        for (index in sent.length() - 1 downTo 0) {
            val item = sent.optJSONObject(index) ?: continue
            CoreBridge.nativeInboxRestoreSent(
                item.optLong("id"), item.optLong("sent"), item.optString("target"),
                item.optString("text"), item.optBoolean("ok"),
            )
        }
        loaded = true
    }

    @Synchronized
    fun addReceived(context: Context, sender: String, text: String, timestamp: String): Long {
        ensureLoaded(context)
        val id = CoreBridge.nativeInboxAddReceived(
            sender, text, timestamp, System.currentTimeMillis() / 1000,
        )
        persist(context)
        return id
    }

    @Synchronized
    fun addSent(context: Context, target: String, text: String, ok: Boolean) {
        ensureLoaded(context)
        CoreBridge.nativeInboxAddSent(target, text, ok, System.currentTimeMillis() / 1000)
        persist(context)
    }

    @Synchronized
    fun setForwarded(context: Context, id: Long, forwarded: Boolean): Boolean {
        ensureLoaded(context)
        val changed = CoreBridge.nativeInboxSetForwarded(id, forwarded)
        if (changed) persist(context)
        return changed
    }

    @Synchronized
    fun delete(context: Context, id: Long): Boolean {
        ensureLoaded(context)
        val deleted = CoreBridge.nativeInboxDelete(id)
        if (deleted) persist(context)
        return deleted
    }

    @Synchronized
    fun json(context: Context, sent: Boolean, limit: Int = 0): String {
        ensureLoaded(context)
        return CoreBridge.nativeInboxJson(sent, limit)
    }

    private fun persist(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString(KEY_RECEIVED, CoreBridge.nativeInboxJson(false, 0))
            .putString(KEY_SENT, CoreBridge.nativeInboxJson(true, 0))
            .apply()
    }
}
