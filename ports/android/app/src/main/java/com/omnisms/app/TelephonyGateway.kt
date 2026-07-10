package com.omnisms.app

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Handler
import android.os.Looper
import android.telephony.SmsManager
import android.telephony.TelephonyManager

object TelephonyGateway {
    fun sendSms(context: Context, target: String, text: String): Result<Unit> {
        if (context.checkSelfPermission(Manifest.permission.SEND_SMS) != PackageManager.PERMISSION_GRANTED) {
            return Result.failure(SecurityException("SEND_SMS permission not granted"))
        }
        return runCatching {
        val manager = context.getSystemService(SmsManager::class.java)
        val parts = manager.divideMessage(text)
        if (parts.size > 1) manager.sendMultipartTextMessage(target, null, parts, null, null)
        else manager.sendTextMessage(target, null, text, null, null)
        }
    }

    fun sendUssd(context: Context, code: String, callback: (Result<String>) -> Unit) {
        if (context.checkSelfPermission(Manifest.permission.CALL_PHONE) != PackageManager.PERMISSION_GRANTED) {
            callback(Result.failure(SecurityException("CALL_PHONE permission not granted")))
            return
        }
        val telephony = context.getSystemService(TelephonyManager::class.java)
        runCatching {
            telephony.sendUssdRequest(code, object : TelephonyManager.UssdResponseCallback() {
                override fun onReceiveUssdResponse(
                    manager: TelephonyManager,
                    request: String,
                    response: CharSequence,
                ) = callback(Result.success(response.toString()))

                override fun onReceiveUssdResponseFailed(
                    manager: TelephonyManager,
                    request: String,
                    failureCode: Int,
                ) = callback(Result.failure(IllegalStateException("USSD failed: $failureCode")))
            }, Handler(Looper.getMainLooper()))
        }.onFailure { callback(Result.failure(it)) }
    }
}
