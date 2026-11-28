package com.sampmobile.game.ui.cef;


import com.sampmobile.launcher.utils.I18n;
import android.content.Context;
import android.util.Log;
import android.webkit.WebResourceResponse;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * 复刻 PC 客户端 CEF custom scheme handler 的 URL 语义：
 *
 *     http://cef/[resourceName]/[relativePath]
 *     cef://[resourceName]/[relativePath]        （PC 端也识别这种写法）
 *
 * PC 侧数据流是：服务端 FileData 分块下发 -> 客户端组装/解密到本地资源包 ->
 * scheme handler 从资源包读取。Android 侧保持同一分段：
 *
 *     服务端下发  ->  CefSession/CefResourceStore 落盘（阶段5）
 *                          |
 *                          v
 *                 <filesDir>/cef/resources/<resource>/<path>
 *                          |
 *                          v
 *                   CefResourceProvider（本类）
 *
 * 本轮（阶段3）只打通 URL 路由与错误反馈：文件存在则原样返回，不存在则返回
 * 明确标注“资源层未同步”的诊断页，避免把空页面误判为网页加载成功。
 * 阶段5 在此处补 OCEF2 解密与 manifest 版本校验，不需要改动调用方。
 */
final class CefResourceProvider {

    private static final String TAG = "CefResourceProvider";

    private CefResourceProvider() {
    }

    static String resourceRootPath(Context context) {
        return new File(context.getFilesDir(), "cef/resources").getAbsolutePath();
    }

    /**
     * @return null 表示不拦截（交给 WebView 默认处理）；非 null 表示本机应答。
     */
    static WebResourceResponse open(Context context, String url) {
        if (url == null || url.length() == 0) return null;

        int schemeEnd = url.indexOf("://");
        if (schemeEnd <= 0) return null;
        String scheme = url.substring(0, schemeEnd).toLowerCase(Locale.US);

        // 去掉 query / fragment：资源路径本身不含这些，但它们会污染文件名。
        String rest = url.substring(schemeEnd + 3);
        int cut = rest.length();
        int q = rest.indexOf('?');
        if (q >= 0 && q < cut) cut = q;
        int f = rest.indexOf('#');
        if (f >= 0 && f < cut) cut = f;
        rest = rest.substring(0, cut);

        int slash = rest.indexOf('/');
        String authority = slash >= 0 ? rest.substring(0, slash) : rest;
        String path = slash >= 0 ? rest.substring(slash + 1) : "";

        // 权威段可能带 userinfo / port，资源路由只关心主机名。
        int at = authority.lastIndexOf('@');
        if (at >= 0) authority = authority.substring(at + 1);
        int colon = authority.indexOf(':');
        if (colon >= 0) authority = authority.substring(0, colon);

        // PC 客户端注册方式（manager.cpp:314）：
        //   CefRegisterSchemeHandlerFactory("http", "cef", ...)
        // 所以真实 URL 是 http://cef/<resource>/<path>；cef:// 也一并接受。
        final boolean isCefUrl =
                "cef".equals(scheme)
                        || (("http".equals(scheme) || "https".equals(scheme))
                        && "cef".equalsIgnoreCase(authority));
        if (!isCefUrl) return null;

        int pathSlash = path.indexOf('/');
        String resourceName = pathSlash >= 0 ? path.substring(0, pathSlash) : path;
        String relativePath = pathSlash >= 0 ? path.substring(pathSlash + 1) : "";
        if (relativePath.length() == 0) {
            // http://cef/favicon.ico 这类请求里 "favicon.ico" 是主机后的第一段，
            // 但它显然是一个文件而不是资源名。CEF 侧只处理 <resource>/<path>，
            // 这里直接 404，不要拿它去套 index.html 再生成诊断页（那会在日志里
            // 制造一条假的 CEF_RESOURCE_MISS，也白白注入一次 bootstrap）。
            if (resourceName.indexOf('.') >= 0) {
                return response("text/plain", 404, "Not Found", new byte[0]);
            }
            relativePath = "index.html";
        }

        if (!isSafeSegment(resourceName) || !isSafeRelativePath(relativePath)) {
            Log.w(TAG, "rejected unsafe resource path: " + url);
            return errorPage(400, "Bad Request", I18n.tr("资源路径非法"));
        }

        // 以 '.' 开头的段是内部标记（例如资源根下的 .cefhash），不对页面开放。
        for (String segment : relativePath.split("/")) {
            if (segment.startsWith(".")) {
                return response("text/plain", 404, "Not Found", new byte[0]);
            }
        }

        File root = new File(context.getFilesDir(), "cef/resources");
        File target = new File(new File(root, resourceName), relativePath);

        // 防御性检查：解析后的路径必须仍在资源根目录内。
        try {
            String rootCanonical = root.getCanonicalPath();
            String targetCanonical = target.getCanonicalPath();
            if (!targetCanonical.startsWith(rootCanonical + File.separator)) {
                Log.w(TAG, "rejected traversal: " + url);
                return errorPage(403, "Forbidden", I18n.tr("资源路径越界"));
            }
        } catch (IOException e) {
            return errorPage(500, "Internal Error", I18n.tr("资源路径解析失败"));
        }

        if (!target.isFile()) {
            Log.i(TAG, "CEF_RESOURCE_MISS " + resourceName + "/" + relativePath);
            return missingPage(resourceName, relativePath);
        }

        try {
            byte[] data = readAll(target);
            // 正常路径下 native（CefResources）已解包并解密，磁盘上是明文；
            // 若仍看到 OCEF2 头，说明解包环节异常（例如手工塞进来的 pak 条目），
            // 不要把它当页面吐给 WebView。
            if (data.length >= 5 && data[0] == 'O' && data[1] == 'C' && data[2] == 'E'
                    && data[3] == 'F' && data[4] == '2') {
                Log.e(TAG, "CEF_RESOURCE_ENCRYPTED " + resourceName + "/" + relativePath
                        + " (" + data.length + " bytes) - still wrapped, extraction failed");
                return errorPage(503, "Resource Encrypted",
                        I18n.tr("该文件仍处于加密封装状态（应当由资源层解密后落盘）"));
            }
            String mime = mimeOf(relativePath);
            if ("text/html".equals(mime)) {
                // window.cef 是 CEF 在 V8 context 创建时注入的；WebView 没有等价的
                // 文档前脚本钩子，所以在字节流里内联 —— 页面脚本执行前对象已就绪，
                // 不存在 cef.on 早于注入的竞态。
                data = CefJs.inject(data);
            }
            Log.i(TAG, "CEF_RESOURCE_HIT " + resourceName + "/" + relativePath
                    + " " + data.length + " bytes, mime=" + mime);
            return response(mime, 200, "OK", data);
        } catch (IOException e) {
            Log.w(TAG, "read failed " + target, e);
            return errorPage(500, "Internal Error", I18n.tr("资源读取失败"));
        }
    }

    private static boolean isSafeSegment(String s) {
        return s.length() > 0 && s.indexOf('\\') < 0 && s.indexOf(':') < 0
                && !s.equals(".") && !s.equals("..");
    }

    private static boolean isSafeRelativePath(String path) {
        if (path.length() == 0) return false;
        if (path.indexOf('\\') >= 0 || path.indexOf(':') >= 0) return false;
        String[] parts = path.split("/");
        for (String part : parts) {
            if (part.equals("..")) return false;
        }
        return true;
    }

    private static byte[] readAll(File file) throws IOException {
        long length = file.length();
        if (length > 64L * 1024 * 1024) {
            throw new IOException("resource too large: " + length);
        }
        byte[] out = new byte[(int) length];
        try (InputStream in = new FileInputStream(file)) {
            int off = 0;
            while (off < out.length) {
                int n = in.read(out, off, out.length - off);
                if (n < 0) break;
                off += n;
            }
            if (off != out.length) {
                byte[] trimmed = new byte[off];
                System.arraycopy(out, 0, trimmed, 0, off);
                return trimmed;
            }
        }
        return out;
    }

    private static WebResourceResponse response(String mime, int status, String reason, byte[] body) {
        Map<String, String> headers = new HashMap<>();
        headers.put("Cache-Control", "no-store");
        if (body != null) {
            headers.put("Content-Length", Integer.toString(body.length));
        }
        InputStream in = new ByteArrayInputStream(body != null ? body : new byte[0]);
        return new WebResourceResponse(mime, "utf-8", status, reason, headers, in);
    }

    private static WebResourceResponse errorPage(int status, String reason, String detail) {
        String html = page(status + " " + reason, detail);
        return response("text/html", status, reason,
                CefJs.inject(html.getBytes(StandardCharsets.UTF_8)));
    }

    private static WebResourceResponse missingPage(String resourceName, String relativePath) {
        String body = page(I18n.tr("资源未同步"),
                I18n.tr("资源 <code>") + escape(resourceName) + "/" + escape(relativePath) + "</code>"
                        + I18n.tr(" 不在本机资源目录中。<br>")
                        + I18n.tr("说明服务端 manifest 未声明该死链，或资源包解包时该条目缺失——")
                        + I18n.tr("检查 <code>CefResources</code> 日志中的 <code>extracted: N file(s) written</code>。"));
        return response("text/html", 503, "Resource Not Downloaded",
                CefJs.inject(body.getBytes(StandardCharsets.UTF_8)));
    }

    private static String page(String title, String detailHtml) {
        return "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                + "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                + "<style>html,body{margin:0;height:100%;background:#0b0f14;color:#e6edf3;"
                + "font:14px/1.6 -apple-system,Roboto,sans-serif}"
                + ".box{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);"
                + "max-width:70%;padding:18px 22px;border:1px solid #30404d;border-radius:10px;"
                + "background:#111820}"
                + "h1{font-size:15px;margin:0 0 8px;color:#ffb454;font-weight:600}"
                + "code{background:#1c2632;padding:1px 5px;border-radius:4px;color:#8fd3ff}"
                + "p{margin:6px 0;color:#a8b6c4}</style></head><body><div class=\"box\">"
                + "<h1>" + escape(title) + "</h1><p>" + detailHtml + "</p></div></body></html>";
    }

    private static String escape(String s) {
        if (s == null) return "";
        return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
                .replace("\"", "&quot;");
    }

    private static String mimeOf(String path) {
        String lower = path.toLowerCase(Locale.US);
        int dot = lower.lastIndexOf('.');
        String ext = dot >= 0 ? lower.substring(dot + 1) : "";
        switch (ext) {
            case "html":
            case "htm":
                return "text/html";
            case "js":
            case "mjs":
                return "application/javascript";
            case "css":
                return "text/css";
            case "json":
                return "application/json";
            case "png":
                return "image/png";
            case "jpg":
            case "jpeg":
                return "image/jpeg";
            case "gif":
                return "image/gif";
            case "webp":
                return "image/webp";
            case "svg":
                return "image/svg+xml";
            case "ico":
                return "image/x-icon";
            case "woff":
                return "font/woff";
            case "woff2":
                return "font/woff2";
            case "ttf":
                return "font/ttf";
            case "otf":
                return "font/otf";
            case "mp3":
                return "audio/mpeg";
            case "ogg":
                return "audio/ogg";
            case "wav":
                return "audio/wav";
            case "mp4":
                return "video/mp4";
            case "webm":
                return "video/webm";
            case "wasm":
                return "application/wasm";
            default:
                return "application/octet-stream";
        }
    }
}
