package com.omnisms.app

import android.util.Base64
import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.charset.StandardCharsets
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory

internal data class SmtpSettings(
    val server: String,
    val port: Int,
    val username: String,
    val password: String,
    val recipient: String,
)

internal enum class SmtpOutcome { SUCCESS, RETRY, PERMANENT_FAILURE }

internal object SmtpClient {
    fun send(settings: SmtpSettings, subject: String, body: String): SmtpOutcome {
        val server = settings.server.trim()
        val from = addressOnly(settings.username)
        val to = addressOnly(settings.recipient.ifBlank { settings.username })
        if (server.isEmpty() || settings.port !in 1..65535 || from.isEmpty() || to.isEmpty() ||
            settings.username.isEmpty() || settings.password.isEmpty()
        ) return SmtpOutcome.PERMANENT_FAILURE

        var session: Session? = null
        return try {
            val active = if (settings.port == 465) {
                Session.connectTls(server, settings.port)
            } else {
                Session.connectPlain(server, settings.port)
            }
            session = active
            if (active.expect(setOf(220)) != SmtpOutcome.SUCCESS) return active.lastOutcome
            if (active.command("EHLO omnisms-android", setOf(250)) != SmtpOutcome.SUCCESS) {
                return active.lastOutcome
            }
            if (settings.port != 465) {
                if (active.command("STARTTLS", setOf(220)) != SmtpOutcome.SUCCESS) {
                    return active.lastOutcome
                }
                active.upgradeTls(server, settings.port)
                if (active.command("EHLO omnisms-android", setOf(250)) != SmtpOutcome.SUCCESS) {
                    return active.lastOutcome
                }
            }
            if (active.command("AUTH LOGIN", setOf(334)) != SmtpOutcome.SUCCESS) return active.lastOutcome
            if (active.command(base64(settings.username), setOf(334)) != SmtpOutcome.SUCCESS) {
                return active.lastOutcome
            }
            if (active.command(base64(settings.password), setOf(235)) != SmtpOutcome.SUCCESS) {
                return active.lastOutcome
            }
            if (active.command("MAIL FROM:<$from>", setOf(250)) != SmtpOutcome.SUCCESS) {
                return active.lastOutcome
            }
            if (active.command("RCPT TO:<$to>", setOf(250, 251)) != SmtpOutcome.SUCCESS) {
                return active.lastOutcome
            }
            if (active.command("DATA", setOf(354)) != SmtpOutcome.SUCCESS) return active.lastOutcome
            active.write(buildMessage(from, to, subject, body))
            if (active.expect(setOf(250)) != SmtpOutcome.SUCCESS) return active.lastOutcome
            active.command("QUIT", setOf(221))
            SmtpOutcome.SUCCESS
        } catch (_: Exception) {
            SmtpOutcome.RETRY
        } finally {
            runCatching { session?.close() }
        }
    }

    private fun base64(value: String): String = Base64.encodeToString(
        value.toByteArray(StandardCharsets.UTF_8), Base64.NO_WRAP,
    )

    private fun addressOnly(raw: String): String {
        var value = headerSafe(raw.trim())
        val left = value.indexOf('<')
        val right = if (left >= 0) value.indexOf('>', left + 1) else -1
        if (left >= 0 && right > left + 1) value = value.substring(left + 1, right).trim()
        return if ('@' in value && ' ' !in value) value else ""
    }

    private fun headerSafe(value: String): String = value.replace('\r', ' ').replace('\n', ' ')

    private fun encodedSubject(subject: String): String {
        val bytes = headerSafe(subject).toByteArray(StandardCharsets.UTF_8)
        if (bytes.isEmpty()) return ""
        val words = mutableListOf<String>()
        var start = 0
        while (start < bytes.size) {
            var end = minOf(start + 45, bytes.size)
            while (end < bytes.size && end > start + 1 && (bytes[end].toInt() and 0xC0) == 0x80) --end
            val chunk = bytes.copyOfRange(start, end)
            words += "=?UTF-8?B?${Base64.encodeToString(chunk, Base64.NO_WRAP)}?="
            start = end
        }
        return words.joinToString("\r\n ")
    }

    private fun wrappedBody(body: String): String {
        val encoded = Base64.encodeToString(body.toByteArray(StandardCharsets.UTF_8), Base64.NO_WRAP)
        if (encoded.isEmpty()) return "\r\n"
        return encoded.chunked(76).joinToString("\r\n", postfix = "\r\n")
    }

    private fun buildMessage(from: String, to: String, subject: String, body: String): String {
        val date = SimpleDateFormat("EEE, dd MMM yyyy HH:mm:ss Z", Locale.US).format(Date())
        return buildString(body.length * 2 + 512) {
            append("From: sms notify <$from>\r\n")
            append("To: <$to>\r\n")
            append("Subject: ${encodedSubject(subject)}\r\n")
            append("Date: $date\r\n")
            append("MIME-Version: 1.0\r\n")
            append("Content-Type: text/plain; charset=UTF-8\r\n")
            append("Content-Transfer-Encoding: base64\r\n\r\n")
            append(wrappedBody(body))
            append(".\r\n")
        }
    }

    private class Session private constructor(private var socket: Socket) {
        private var reader = BufferedReader(InputStreamReader(socket.getInputStream(), StandardCharsets.US_ASCII))
        private var writer = BufferedWriter(OutputStreamWriter(socket.getOutputStream(), StandardCharsets.US_ASCII))
        var lastOutcome: SmtpOutcome = SmtpOutcome.RETRY
            private set

        fun command(command: String, expected: Set<Int>): SmtpOutcome {
            write("$command\r\n")
            return expect(expected)
        }

        fun write(value: String) {
            writer.write(value)
            writer.flush()
        }

        fun expect(expected: Set<Int>): SmtpOutcome {
            val code = readResponseCode()
            lastOutcome = when {
                code in expected -> SmtpOutcome.SUCCESS
                code in 500..599 -> SmtpOutcome.PERMANENT_FAILURE
                else -> SmtpOutcome.RETRY
            }
            return lastOutcome
        }

        fun upgradeTls(host: String, port: Int) {
            val factory = SSLSocketFactory.getDefault() as SSLSocketFactory
            val ssl = factory.createSocket(socket, host, port, true) as SSLSocket
            configureTls(ssl)
            ssl.startHandshake()
            socket = ssl
            reader = BufferedReader(InputStreamReader(ssl.getInputStream(), StandardCharsets.US_ASCII))
            writer = BufferedWriter(OutputStreamWriter(ssl.getOutputStream(), StandardCharsets.US_ASCII))
        }

        fun close() = socket.close()

        private fun readResponseCode(): Int {
            var lines = 0
            var code = -1
            while (lines++ < 64) {
                val line = reader.readLine() ?: return -1
                if (line.length < 3) return -1
                val parsed = line.substring(0, 3).toIntOrNull() ?: return -1
                code = parsed
                if (line.length == 3 || line[3] == ' ') return code
                if (line[3] != '-') return -1
            }
            return code
        }

        companion object {
            private const val CONNECT_TIMEOUT_MS = 15_000
            private const val IO_TIMEOUT_MS = 15_000

            fun connectPlain(host: String, port: Int): Session {
                val socket = Socket()
                socket.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)
                socket.soTimeout = IO_TIMEOUT_MS
                return Session(socket)
            }

            fun connectTls(host: String, port: Int): Session {
                val transport = Socket()
                transport.connect(InetSocketAddress(host, port), CONNECT_TIMEOUT_MS)
                transport.soTimeout = IO_TIMEOUT_MS
                val factory = SSLSocketFactory.getDefault() as SSLSocketFactory
                val socket = factory.createSocket(transport, host, port, true) as SSLSocket
                socket.soTimeout = IO_TIMEOUT_MS
                configureTls(socket)
                socket.startHandshake()
                return Session(socket)
            }

            private fun configureTls(socket: SSLSocket) {
                val parameters = socket.sslParameters
                parameters.endpointIdentificationAlgorithm = "HTTPS"
                socket.sslParameters = parameters
            }
        }
    }
}
