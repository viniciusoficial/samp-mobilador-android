package com.sampmobile.launcher;


import com.sampmobile.launcher.utils.I18n;
import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;

import com.joom.paranoid.Obfuscate;

import java.util.List;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import pub.devrel.easypermissions.EasyPermissions;
import com.sampmobile.launcher.utils.ClientSettings;
import com.sampmobile.launcher.utils.LocaleManager;
import com.sampmobile.launcher.utils.Logcat;
import com.sampmobile.launcher.utils.Utils;

@Obfuscate
public class SplashActivity extends AppCompatActivity implements GLSurfaceView.Renderer, EasyPermissions.PermissionCallbacks {
    private static final int PERMISSION_REQUEST_CODE = 1337;

    private static String[] getRequiredPermissions() {
        if (Build.VERSION.SDK_INT <= 32) {
            return new String[]{
                    Manifest.permission.WRITE_EXTERNAL_STORAGE,
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.RECORD_AUDIO
            };
        }
        return new String[]{
                Manifest.permission.RECORD_AUDIO
        };
    }

    private boolean mAppStarted = false;
    private final Handler mStartHandler = new Handler(Looper.getMainLooper());
    GLSurfaceView glSurfaceView;
    Logcat logcat;

    @Override
    protected void attachBaseContext(Context newBase) {
        super.attachBaseContext(LocaleManager.wrap(newBase));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_splash);

        getExternalFilesDir(null);
        getObbDir();
        getExternalMediaDirs();

        logcat = new Logcat(this);
        logcat.start();

        glSurfaceView = findViewById(R.id.surface);
        glSurfaceView.setRenderer(this);

        ((TextView) findViewById(R.id.version_text)).setText("v" + BuildConfig.VERSION_NAME);

        String[] requiredPermissions = getRequiredPermissions();
        if (EasyPermissions.hasPermissions(this, requiredPermissions)) {
            startApp();
        } else {
            EasyPermissions.requestPermissions(this, I18n.tr("请允许应用所需权限"), PERMISSION_REQUEST_CODE, requiredPermissions);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (glSurfaceView != null) {
            glSurfaceView.onPause();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (glSurfaceView != null) {
            glSurfaceView.onResume();
        }
    }

    @Override
    protected void onDestroy() {
        mStartHandler.removeCallbacksAndMessages(null);
        super.onDestroy();
        if (logcat != null) {
            logcat.stop();
            logcat = null;
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        EasyPermissions.onRequestPermissionsResult(requestCode, permissions, grantResults, this);
    }

    @Override
    public void onPermissionsGranted(int requestCode, @NonNull List<String> list) {
        if (requestCode == PERMISSION_REQUEST_CODE) {
            startApp();
        }
    }

    @Override
    public void onPermissionsDenied(int requestCode, @NonNull List<String> list) {
        if (requestCode == PERMISSION_REQUEST_CODE) {
            startApp();
        }
    }

    private void startApp() {
        if (mAppStarted || isFinishing() || isDestroyed()) {
            return;
        }
        if (!Utils.isOnline(this)) {
            new AlertDialog.Builder(this)
                    .setTitle(I18n.tr("没有网络连接！"))
                    .setMessage(I18n.tr("请连接网络后重试。"))
                    .setPositiveButton(I18n.tr("重试"), (dialog, which) -> {
                        dialog.dismiss();
                        startApp();
                    }).setNegativeButton(I18n.tr("单机模式"), (dialog, which) -> {
                        if (isFinishing() || isDestroyed()) return;
                        AlertDialog alertDialog = new AlertDialog.Builder(this)
                                .setTitle(R.string.offline_mode)
                                .setMessage(I18n.tr("确定要以单机模式启动游戏吗？"))
                                .setPositiveButton(I18n.tr("是"), (dialog2, which2) -> {
                                    ClientSettings.prepareOfflineLaunch(this);
                                    try {
                                        Intent intent = new Intent(this, com.sampmobile.game.SAMP.class);
                                        intent.putExtra("extra_check", "sampmobile1337");
                                        startActivity(intent);
                                    } catch (Exception e) {
                                        e.printStackTrace();
                                    }
                                })
                                .setNegativeButton(I18n.tr("否"), (dialog2, which2) -> {
                                    dialog2.dismiss();
                                    startApp();
                                })
                                .setCancelable(false)
                                .create();
                        alertDialog.show();
                    }).setCancelable(false).show();
            return;
        }

        mAppStarted = true;
        ClientSettings.ensureExists(this);
        mStartHandler.postDelayed(() -> {
            if (isFinishing() || isDestroyed()) return;
            startMain();
        }, 2500);
    }

    private void startMain() {
        if (!isFinishing()) {
            try {
                Intent intent = new Intent(SplashActivity.this, MainActivity.class);
                intent.putExtra("extra_check", "sampmobile1337");
                startActivity(intent);
                finish();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    @Override
    public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
        String renderer = gl10.glGetString(gl10.GL_RENDERER);
        Log.i("SplashActivity", "Renderer: " + renderer);

        String extensions = gl10.glGetString(gl10.GL_EXTENSIONS);
        Log.i("SplashActivity", "Extensions: " + extensions);
        if (extensions == null) {
            extensions = "";
        }

        Utils.GPUType type;

        if (extensions.contains("GL_IMG_texture_compression_pvrtc")) {
            type = Utils.GPUType.PVR;
        } else if (extensions.contains("GL_EXT_texture_compression_dxt1") || extensions.contains("GL_EXT_texture_compression_s3tc") || extensions.contains("GL_AMD_compressed_ATC_texture")) {
            type = Utils.GPUType.DXT;
        } else {
            type = Utils.GPUType.ETC;
        }

        Utils.GPU_TYPE = type;
        Log.i("SplashActivity", "GPU_TYPE: " + Utils.GPU_TYPE.name());

        int error = gl10.glGetError();
        if (error != gl10.GL_NO_ERROR) {
            Log.e("SplashActivity", "OpenGL error: " + error);
        }

        runOnUiThread(() -> {
            if (!isFinishing() && !isDestroyed() && glSurfaceView != null) {
                glSurfaceView.setVisibility(View.GONE);
            }
        });
    }

    @Override
    public void onSurfaceChanged(GL10 gl10, int i, int i1) {
    }

    @Override
    public void onDrawFrame(GL10 gl10) {
    }

    @Override
    public void onBackPressed() {
        quitApp();
    }

    private long quit_time = 0;

    public void quitApp() {
        if ((System.currentTimeMillis() - quit_time) > 2000) {
            Toast.makeText(this, I18n.tr("再按一次退出"), Toast.LENGTH_LONG).show();
            quit_time = System.currentTimeMillis();
        } else {
            finish();
        }
    }
}
