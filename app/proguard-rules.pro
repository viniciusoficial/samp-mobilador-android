-keep class com.nvidia.devtech.* { *; }
-keep class com.rockstargames.gtasa.* { *; }
-keep class com.wardrumstudios.utils.* { *; }

-keep class com.sampmobile.game.* { *; }
-keep class com.sampmobile.game.ui.* { *; }
-keep class com.sampmobile.game.ui.widgets.* { *; }
-keep class com.sampmobile.game.ui.widgets.adapter.* { *; }
# omp-cef 浏览器层：CefManager 的类名/方法名被 JNI 直接按字符串查找
-keep class com.sampmobile.game.ui.cef.* { *; }

# for minify
-dontwarn javax.servlet.**
-dontwarn org.conscrypt.**
-dontwarn org.bouncycastle.**
-dontwarn org.openjsse.**