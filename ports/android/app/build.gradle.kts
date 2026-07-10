plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.omnisms.app"
    compileSdk = 36
    ndkVersion = "28.1.13356709"

    defaultConfig {
        applicationId = "com.omnisms.app"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-Wall", "-Wextra")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions.jvmTarget = "17"
}

dependencies {
    implementation("androidx.core:core-ktx:1.16.0")
    implementation("androidx.work:work-runtime-ktx:2.10.1")
}

val verifyWebStyleTokens by tasks.registering {
    group = "verification"
    description = "Ensure Android native colors stay aligned with webui/app.css tokens."
    inputs.file(file("../../../webui/app.css"))
    inputs.file(file("src/main/res/values/colors.xml"))
    doLast {
        val css = file("../../../webui/app.css").readText()
        val colors = file("src/main/res/values/colors.xml").readText()
        val mappings = linkedMapOf(
            "bg" to "omni_bg",
            "canvas" to "omni_canvas",
            "canvas-soft" to "omni_canvas_soft",
            "inset" to "omni_inset",
            "hairline" to "omni_hairline",
            "hairline-strong" to "omni_hairline_strong",
            "ink" to "omni_ink",
            "body" to "omni_body",
            "mute" to "omni_mute",
            "faint" to "omni_faint",
            "amber" to "omni_amber",
            "amber-dim" to "omni_amber_dim",
            "error" to "omni_error",
        )
        mappings.forEach { (webName, androidName) ->
            val web = Regex("--${Regex.escape(webName)}:\\s*(#[0-9A-Fa-f]{6})")
                .find(css)?.groupValues?.get(1)
                ?: error("Missing Web style token --$webName")
            val android = Regex("name=\"${Regex.escape(androidName)}\">(#[0-9A-Fa-f]{6})<")
                .find(colors)?.groupValues?.get(1)
                ?: error("Missing Android color $androidName")
            check(web.equals(android, ignoreCase = true)) {
                "Style token drift: --$webName=$web but $androidName=$android"
            }
        }
    }
}

tasks.named("preBuild").configure { dependsOn(verifyWebStyleTokens) }
