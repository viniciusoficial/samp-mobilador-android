package com.sampmobile.game.ui.cef;


import com.sampmobile.launcher.utils.I18n;
import android.util.Log;

import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * window.cef 页面侧实现的注入源。
 *
 * 对应 omp-cef src/client/renderer/main.cpp 中的 renderer 侧 JS API：
 *
 *   cef.emit(eventName, ...args)   页面 -> 服务端
 *   cef.on(eventName, callback)    服务端 -> 页面（回调）
 *   cef.off([eventName[, callback]])
 *   cef.set_focus(hasFocus)
 *
 * 语义差异只在参数类型：原版走 CefListValue（bool/int/double/string/null），
 * 这里用 JSON.stringify 把标量送过 JNI，CefJson 再按字面量还原类型
 * （"1" -> Integer，"1.5" -> Float，"true" -> Bool），阶段4 的设备端测试会
 * 逐类型校验字节级一致。
 *
 * 注入方式（关键）：
 *   1. 我们对 http://cef/ 下的 HTML 拥有完整控制权，CefResourceProvider 在
 *      返回字节流时直接插入 <script>，页面自身脚本执行前 window.cef 已存在，
 *      没有竞态 —— 这等价于 CEF 在 V8 context 创建时就挂好对象。
 *   2. 远程 URL（服务端 LoadUrl 到外站）无法改字节流，退化为 onPageStarted /
 *      onPageFinished 两次 evaluateJavascript 补注入。这种情况下页面自己的内联
 *      脚本若在补注入之前调用 cef.on，会因 window.cef 还不存在而抛错 —— 这是
 *      WebView 侧无法在文档解析前执行脚本的固有限制，测试页（走 1）不受影响。
 *
 * 脚本必须是纯 ASCII：它可能出现在 <meta charset> 之前，非 ASCII 字节会被
 * 当成页面声明的编码解析而乱码。
 */
final class CefJs {

    private static final String TAG = "CefJs";

    /** 桥对象名：window.cefNative（由 CefWebView.addJavascriptInterface 提供）。 */
    static final String BRIDGE_NAME = "cefNative";

    /** 已注入标记，避免重复注入覆盖页面已经注册的回调。 */
    private static final String MARKER = "__ompcef_v1";

    static final String BOOTSTRAP =
            "(function(){\n"
            + "  if (window.cef && window.cef.__mark === '" + MARKER + "') return;\n"
            + "  var handlers = Object.create(null);\n"
            + "  var pending  = Object.create(null);\n"
            + "  var MAX_PENDING = 256;\n"
            + "\n"
            + "  function invoke(list, args) {\n"
            + "    for (var i = 0; i < list.length; i++) {\n"
            + "      try { list[i].apply(null, args); }\n"
            + "      catch (e) { if (window.console) console.error('cef handler error', e); }\n"
            + "    }\n"
            + "  }\n"
            + "\n"
            + "  function dispatch(name, args) {\n"
            + "    var list = handlers[name];\n"
            + "    if (list && list.length) { invoke(list, args); return; }\n"
            + "    var q = pending[name] || (pending[name] = []);\n"
            + "    q.push(args);\n"
            + "    if (q.length > MAX_PENDING) q.shift();\n"
            + "  }\n"
            + "\n"
            + "  function flush(name) {\n"
            + "    var q = pending[name];\n"
            + "    if (!q || !q.length) return;\n"
            + "    delete pending[name];\n"
            + "    var list = handlers[name];\n"
            + "    if (!list) return;\n"
            + "    for (var i = 0; i < q.length; i++) invoke(list, q[i]);\n"
            + "  }\n"
            + "\n"
            + "  function bridge() {\n"
            + "    return (typeof window." + BRIDGE_NAME + " === 'object') ? window." + BRIDGE_NAME + " : null;\n"
            + "  }\n"
            + "\n"
            + "  var api = {\n"
            + "    __mark: '" + MARKER + "',\n"
            + "\n"
            + "    emit: function(name) {\n"
            + "      if (typeof name !== 'string')\n"
            + "        throw new TypeError('Invalid arguments to cef.emit(eventName, ...args)');\n"
            + "      var out = [];\n"
            + "      for (var i = 1; i < arguments.length; i++) {\n"
            + "        var v = arguments[i];\n"
            + "        var t = typeof v;\n"
            + I18n.tr("        // 与原版一致：只有 bool / number / string 过线，其余传 null\n")
            + I18n.tr("        // 并在 browser 进程侧被丢弃。\n")
            + "        if (t === 'boolean' || t === 'number' || t === 'string') out.push(v);\n"
            + "        else out.push(null);\n"
            + "      }\n"
            + "      var b = bridge();\n"
            + "      if (!b) { if (window.console) console.warn('cef: bridge unavailable'); return; }\n"
            + "      b.emit(name, JSON.stringify(out));\n"
            + "    },\n"
            + "\n"
            + "    on: function(name, callback) {\n"
            + "      if (typeof name !== 'string' || typeof callback !== 'function')\n"
            + "        throw new TypeError('Invalid arguments to cef.on(eventName, callback)');\n"
            + "      (handlers[name] || (handlers[name] = [])).push(callback);\n"
            + "      flush(name);\n"
            + "    },\n"
            + "\n"
            + "    off: function(name, callback) {\n"
            + "      if (arguments.length === 0) {\n"
            + "        handlers = Object.create(null);\n"
            + "        pending  = Object.create(null);\n"
            + "        return;\n"
            + "      }\n"
            + "      if (typeof name !== 'string')\n"
            + "        throw new TypeError('Invalid arguments to cef.off(eventName[, callback])');\n"
            + "      if (arguments.length === 1) { delete handlers[name]; delete pending[name]; return; }\n"
            + "      if (typeof callback !== 'function')\n"
            + "        throw new TypeError('Invalid arguments to cef.off(eventName[, callback])');\n"
            + "      var list = handlers[name];\n"
            + "      if (!list) return;\n"
            + "      for (var i = list.length - 1; i >= 0; i--) if (list[i] === callback) list.splice(i, 1);\n"
            + "      if (!list.length) delete handlers[name];\n"
            + "    },\n"
            + "\n"
            + "    set_focus: function(hasFocus) {\n"
            + "      if (typeof hasFocus !== 'boolean')\n"
            + "        throw new TypeError('Invalid arguments to cef.set_focus(hasFocus: boolean)');\n"
            + "      var b = bridge();\n"
            + "      if (b && b.setFocus) b.setFocus(hasFocus);\n"
            + "    }\n"
            + "  };\n"
            + "\n"
            + "  window.__cefDispatch = dispatch;\n"
            + "  try { Object.defineProperty(window, 'cef', { value: api, writable: false, configurable: true }); }\n"
            + "  catch (e) { window.cef = api; }\n"
            // 输入框焦点跟踪：原版在 OnAfterCreated 里注入同一段逻辑（client.cpp），
            // 用于让客户端区分"页面拿到了键盘"和"浏览器有焦点"两件事。
            + "  try {\n"
            + "    var isFocused = false;\n"
            + "    var checkFocus = function (target) {\n"
            + "      var tag = (target && target.tagName) ? target.tagName : '';\n"
            + "      var isInput = (tag === 'INPUT' || tag === 'TEXTAREA' || (target && target.isContentEditable));\n"
            + "      if (isInput !== isFocused) {\n"
            + "        isFocused = isInput;\n"
            + "        api.emit('omp:cef:internal:inputFocusChanged', isFocused);\n"
            + "      }\n"
            + "    };\n"
            + "    document.addEventListener('focusin', function (e) { checkFocus(e.target); }, true);\n"
            + "    document.addEventListener('focusout', function () {\n"
            + "      if (isFocused) { isFocused = false; api.emit('omp:cef:internal:inputFocusChanged', false); }\n"
            + "    }, true);\n"
            + "    document.addEventListener('mousedown', function (e) { checkFocus(e.target); }, true);\n"
            + "  } catch (e) {}\n"
            + "  if (window.console) console.log('cef: window.cef ready (" + MARKER + ")');\n"
            + "})();\n";

    private CefJs() {
    }

    /** 服务端事件 -> JS 分发调用。name 用 JSON 转义，argsJson 已是数组字面量。 */
    static String dispatchScript(String name, String argsJson) {
        String safeArgs = (argsJson == null || argsJson.length() == 0) ? "[]" : argsJson;
        return "if(typeof window.__cefDispatch==='function'){window.__cefDispatch("
                + quote(name) + "," + safeArgs + ");}";
    }

    /**
     * 把 bootstrap 插进 HTML 字节流的 <head> 之后。
     *
     * 全程按字节处理：既避免 UTF-8 解码/重编码破坏非 UTF-8 页面，也避免
     * charset 声明前后顺序带来的编码歧义（脚本本身是 ASCII）。
     */
    static byte[] inject(byte[] html) {
        if (html == null || html.length == 0)
            return html;

        if (indexOf(html, MARKER.getBytes(StandardCharsets.US_ASCII), 0) >= 0) {
            return html; // 页面自带 bootstrap
        }

        byte[] script = ("<script>" + BOOTSTRAP + "</script>")
                .getBytes(StandardCharsets.US_ASCII);

        int insertAt = afterTag(html, "head");
        if (insertAt < 0) insertAt = afterTag(html, "html");
        if (insertAt < 0) insertAt = 0;

        byte[] out = new byte[html.length + script.length];
        System.arraycopy(html, 0, out, 0, insertAt);
        System.arraycopy(script, 0, out, insertAt, script.length);
        System.arraycopy(html, insertAt, out, insertAt + script.length, html.length - insertAt);
        Log.i(TAG, "bootstrap injected at byte " + insertAt + " (page " + html.length + " B)");
        return out;
    }

    /** 返回 "<head ...>" 之后的下标；找不到返回 -1。ASCII 大小写不敏感。 */
    private static int afterTag(byte[] data, String tag) {
        int i = 0;
        while (i < data.length) {
            if (data[i] == '<') {
                int j = i + 1;
                if (j < data.length && data[j] == '/') { i = j + 1; continue; }
                int k = 0;
                while (k < tag.length() && j + k < data.length) {
                    int c = data[j + k] & 0xFF;
                    if (c >= 'A' && c <= 'Z') c += 32;
                    if (c != tag.charAt(k)) break;
                    k++;
                }
                if (k == tag.length()) {
                    // 必须是 <head 而不是 <header ...
                    int after = j + k;
                    if (after >= data.length) return data.length;
                    int c = data[after] & 0xFF;
                    if (c == '>' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/') {
                        for (int p = after; p < data.length; p++) {
                            if (data[p] == '>') return p + 1;
                        }
                        return data.length;
                    }
                }
            }
            i++;
        }
        return -1;
    }

    private static int indexOf(byte[] haystack, byte[] needle, int from) {
        if (needle.length == 0 || haystack.length < needle.length) return -1;
        outer:
        for (int i = Math.max(0, from); i + needle.length <= haystack.length; i++) {
            for (int j = 0; j < needle.length; j++) {
                if (haystack[i + j] != needle[j]) continue outer;
            }
            return i;
        }
        return -1;
    }

    private static String quote(String s) {
        StringBuilder sb = new StringBuilder(s == null ? 2 : s.length() + 2);
        sb.append('"');
        for (int i = 0; s != null && i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"':  sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20 || c == 0x2028 || c == 0x2029) {
                        sb.append(String.format(Locale.US, "\\u%04x", (int) c));
                    } else {
                        sb.append(c);
                    }
                    break;
            }
        }
        sb.append('"');
        return sb.toString();
    }
}
