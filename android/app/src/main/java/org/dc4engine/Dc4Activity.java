package org.dc4engine;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.*;
import java.util.ArrayList;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.concurrent.ConcurrentLinkedQueue;

/** Android presentation/input shell; scene execution stays in dc4core. */
public class Dc4Activity extends Activity implements SurfaceHolder.Callback {
    private static final String TAG = "dc4";
    private static final int W = 1280;
    private static final int H = 720;
    private static final Charset SJIS = Charset.forName("windows-31j");

    static { System.loadLibrary("dc4"); }

    private native boolean dc4Init(String dataDir);
    private native void dc4Quit();
    private native void dc4Configure(String saveDir, String overlayDir, String textEncoding, String fontPath, String language);
    private native String dc4OpenMenu(String name);
    private native boolean dc4MenuActive();
    private native boolean dc4MenuFrame(Bitmap bitmap);
    private native String dc4MenuTouch(int action, int x, int y);
    private native String dc4PlaybackSettings();
    private native String dc4ValidateSaveFile(String base, String overlay, String encoding, String path);
    private native boolean dc4Advance();
    private native void dc4Render(Bitmap bitmap);
    private native String dc4CurrentLine();
    private native String dc4Touch(int action, int x, int y);
    private native String dc4SetPaused(boolean paused, String checkpoint);
    private native String dc4Status();
    private native String dc4Speaker();
    private native boolean dc4SkipAdvance();
    private native String dc4Save(String path);
    private native String dc4Load(String path);
    private native void dc4RenderPreview(Bitmap bitmap);
    private native void dc4LogOpen();
    private native boolean dc4LogFrame(Bitmap bitmap);
    private native String dc4LogTouch(int action, int x, int y);
    private native void dc4LogClear();
    private native String dc4SlotsOpen(boolean loading, boolean latest, boolean quick);
    private native void dc4SlotInfo(int index, boolean occupied, String timestamp,
                                   String excerpt, Bitmap thumbnail);
    private native boolean dc4SlotsFrame(Bitmap bitmap);
    private native String dc4SlotsTouch(int action, int x, int y);
    private native String dc4RequestConfirm(String action);
    private native boolean dc4ConfirmActive();
    private native boolean dc4CancelConfirmation();
    private native void dc4ConfirmFrame(Bitmap bitmap);
    private native String dc4ConfirmTouch(int action, int x, int y);

    private SurfaceView surface;
    private FrameLayout gameLayout;
    private TextView bubble;
    private GameLibrary library;
    private GameLibrary.Entry selected;
    private SharedPreferences preferences;
    private String uiLanguage = "en";
    private volatile boolean ioBusy;
    private boolean inLibrary = true, destroyed;
    private final ExecutorService fileWorker = Executors.newSingleThreadExecutor();
    private AtomicBoolean importCancelled;
    private AlertDialog progressDialog;
    private TextView progressMessage;
    private View launchOverlay;
    private boolean firstFramePending;
    private AlertDialog settingsDialog;
    private GameLibrary.Entry documentGame;
    private String documentLanguage;
    private String pendingOverlayLanguage;
    private static final int PICK_GAME = 20, PICK_OVERLAY = 21, PICK_FONT = 22, EXPORT_SAVES = 23, IMPORT_SAVES = 24;
    private static final int TEXT = ArisaUi.TEXT, MUTED = ArisaUi.MUTED;
    private File savesDir() {
        return new File(getFilesDir(), "games/" + selected.id + "/saves/" + selected.language);
    }
    private File savesDir(GameLibrary.Entry game, String language) {
        return new File(getFilesDir(), "games/" + game.id + "/saves/" + language);
    }
    private String tr(String en, String id, String jp) { return uiLanguage.equals("id") ? id : uiLanguage.equals("jp") ? jp : en; }
    private int dp(float value) { return (int) (value * getResources().getDisplayMetrics().density + .5f); }
    private Bitmap frame;
    private Thread renderThread;
    private volatile boolean running;
    // Lifecycle flags are owned by the UI thread, not the render thread.
    private boolean resumed;
    private boolean surfaceReady;
    private boolean initialized;
    private boolean initializationFailed;
    private boolean awaitingStorageAccess;
    private String dataDir;
    private String pauseCheckpoint;
    private int activePointerId = -1;
    private final ConcurrentLinkedQueue<Touch> touches = new ConcurrentLinkedQueue<>();
    // Dialog callbacks enqueue work; every native call still runs on the render thread.
    private final ConcurrentLinkedQueue<Runnable> menuCommands = new ConcurrentLinkedQueue<>();

    // Shell UI state, owned by the render thread; the volatile flags are also
    // cleared from onPause (UI thread) so no mode survives the background.
    private volatile boolean autoMode, skipMode, backlogOpen, slotMenuOpen;
    private boolean slotMenuLoading;
    private int lastLineSerial = -1;
    private long revealedAt;
    private boolean wasRevealed, choiceNotified, wasEnded;
    private long lastSkipTick;
    private static final int SLOT_COUNT = 100;
    private int latestSlot = -1;
    private int pendingSaveSlot = -1, saveRequestToken;
    private AlertDialog saveDialog;  // UI thread only.
    private Bitmap previewBitmap;
    private Paint badgePaint, badgeBgPaint;
    private SimpleDateFormat dateFormat;

    // Parsed dc4Status fields.
    private int stLine;
    private boolean stChoice, stVoice, stAnim, stRevealed, stVisible, stEnded;
    private int autoDelayMs = 1200;
    private boolean waitVoice = true, stopAutoChoice = true, stopSkipChoice = true, skipAllowed = true;

    private static final class Touch {
        final int action, x, y;
        Touch(int action, int x, int y) { this.action = action; this.x = x; this.y = y; }
    }

    /** MS932, matching Windows CP932 rather than strict JIS-only decoding. */
    public String decodeSjis(byte[] bytes) { return new String(bytes, SJIS); }

    private void notifyUser(String message) {
        if (message != null && !message.isEmpty())
            runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_LONG).show());
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        preferences = getSharedPreferences("launcher", MODE_PRIVATE);
        uiLanguage = preferences.getString("uiLanguage", "en");
        try { library = new GameLibrary(getFilesDir()); }
        catch (IOException error) { notifyUser(error.getMessage()); finish(); return; }
        frame = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
        showLibrary();
        if (library.entries.isEmpty()) {
            File found = resolveDataDir();
            if (found != null) registerPath(found, null, null, false);
        }
    }

    private void createGameSurface() {
        gameLayout = new FrameLayout(this);
        gameLayout.setBackgroundColor(Color.BLACK);
        gameLayout.setFitsSystemWindows(true);
        surface = new SurfaceView(this);
        surface.getHolder().addCallback(this);
        // Do not setFixedSize: a fixed 16:9 buffer is stretched by SurfaceView
        // on other aspect ratios and breaks inverse-mapped choice hit testing.
        surface.setOnTouchListener((v, event) -> {
            // Waiting on the storage grant: a tap re-opens the settings screen.
            if (!running && awaitingStorageAccess) {
                if (event.getActionMasked() == MotionEvent.ACTION_UP) ensureStorageAccess();
                return true;
            }
            if (!running) return true;
            int action = event.getActionMasked();
            // Only the finger that began the gesture participates.
            if (action == MotionEvent.ACTION_DOWN) activePointerId = event.getPointerId(0);
            if (action == MotionEvent.ACTION_POINTER_UP &&
                    event.getPointerId(event.getActionIndex()) == activePointerId)
                action = MotionEvent.ACTION_CANCEL;
            if (action != MotionEvent.ACTION_DOWN && action != MotionEvent.ACTION_UP &&
                    action != MotionEvent.ACTION_MOVE && action != MotionEvent.ACTION_CANCEL) return true;
            int index = event.findPointerIndex(activePointerId);
            if (index < 0) action = MotionEvent.ACTION_CANCEL;
            RectF viewport = viewport(surface.getWidth(), surface.getHeight());
            int x = -1, y = -1;
            if (index >= 0 && viewport.contains(event.getX(index), event.getY(index))) {
                x = (int) ((event.getX(index) - viewport.left) * W / viewport.width());
                y = (int) ((event.getY(index) - viewport.top) * H / viewport.height());
            }
            touches.offer(new Touch(action, x, y));
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) activePointerId = -1;
            if (action == MotionEvent.ACTION_UP) v.performClick();
            return true;
        });
        gameLayout.addView(surface, new FrameLayout.LayoutParams(-1, -1));
        addSettingsBubble();
        showLaunchOverlay(null);
        setContentView(gameLayout);
    }

    /** The same centered 16:9 rectangle is used by painting and touch mapping. */
    private static RectF viewport(int width, int height) {
        float scale = Math.min(width / (float) W, height / (float) H);
        float w = W * scale, h = H * scale;
        return new RectF((width - w) / 2, (height - h) / 2,
                (width + w) / 2, (height + h) / 2);
    }

    /**
     * Finds DC4/AdvData on shared storage without assuming letter case; Android
     * filesystems are case-sensitive and file managers rename freely. Needs
     * all-files access on Android 11+ (or the legacy read permission below).
     */
    private File findSharedDataDir() {
        File root = Environment.getExternalStorageDirectory();
        File[] entries = root == null ? null : root.listFiles();
        if (entries != null)
            for (File entry : entries) {
                if (!entry.isDirectory() || !entry.getName().equalsIgnoreCase("DC4")) continue;
                File hit = probeDir(new File(entry, "AdvData"));
                if (hit != null) return hit;
                File[] inner = entry.listFiles();
                if (inner != null)
                    for (File i : inner)
                        if (i.isDirectory() && i.getName().equalsIgnoreCase("AdvData")) {
                            hit = probeDir(i);
                            if (hit != null) return hit;
                        }
            }
        return null;
    }

    /**
     * A candidate counts only if the required startup scripts are readable:
     * scoped storage still lets an unprivileged app stat() shared-storage
     * paths, so isDirectory() alone reports folders it cannot actually open.
     */
    private static File probeDir(File dir) {
        if (!new File(dir, "MES").isDirectory()) return null;
        try (InputStream in = new FileInputStream(new File(dir, "MES/start.mes"))) {
            if (in.read() < 0) return null;
        } catch (IOException error) {
            return null;
        }
        return dir;
    }

    /** Null when no readable AdvData exists yet, so the caller can ask for access. */
    private File resolveDataDir() {
        String extra = getIntent().getStringExtra("data_dir");
        if (extra != null) {
            File hit = probeDir(new File(extra));
            if (hit != null) return hit;
        }
        File external = getExternalFilesDir(null);
        if (external != null) {
            File hit = probeDir(new File(external, "AdvData"));
            if (hit != null) return hit;
        }
        File hit = probeDir(new File(getFilesDir(), "AdvData"));
        if (hit != null) return hit;
        hit = probeDir(new File("/storage/emulated/0/DC4/AdvData"));
        if (hit != null) return hit;
        hit = probeDir(new File("/sdcard/DC4/AdvData"));
        if (hit != null) return hit;
        return findSharedDataDir();
    }

    private File defaultDataDir() {
        File external = getExternalFilesDir(null);
        return new File(external != null ? external : getFilesDir(), "AdvData");
    }

    /** True when the app is allowed to read arbitrary shared-storage paths. */
    private boolean storageAccessGranted() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * The app-specific folders always work; shared storage (DC4/AdvData) needs
     * the user's explicit grant. Returns false when access still must be given.
     */
    private boolean ensureStorageAccess() {
        if (storageAccessGranted()) return true;
        awaitingStorageAccess = true;
        if (Build.VERSION.SDK_INT >= 30) {
            notifyUser(tr("Allow Arisa32 to read your game folder.", "Izinkan Arisa32 membaca folder game Anda.", "Arisa32 にゲームフォルダーの読み取りを許可してください。"));
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName()));
            try {
                startActivity(intent);
            } catch (RuntimeException error) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            notifyUser(tr("Allow Arisa32 to read your game folder.", "Izinkan Arisa32 membaca folder game Anda.", "Arisa32 にゲームフォルダーの読み取りを許可してください。"));
            requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, 1);
        }
        return false;
    }

    // Starting/stopping is always on the UI thread. No JNI call can race quit:
    // native lifecycle access happens only before start or after a full join.
    private void startRendering() {
        if (!resumed || inLibrary || ioBusy || selected == null || !surfaceReady || renderThread != null || initializationFailed) return;
        if (!initialized) {
            dataDir = selected.path;
            pauseCheckpoint = new File(getFilesDir(), "games/" + selected.id + "/pause-" + selected.language + ".dc4save").getAbsolutePath();
            try { SaveArchive.recover(savesDir()); }
            catch (IOException error) { initializationFailed = true; showLaunchOverlay(error.getMessage()); return; }
            savesDir().mkdirs();
            dc4Configure(savesDir().getAbsolutePath(), selected.overlay(), selected.encoding(), selected.font, selected.language);
            Log.i(TAG, "data dir: " + dataDir);
            initialized = dc4Init(dataDir);
            if (!initialized) {
                initializationFailed = true;
                Log.e(TAG, "dc4Init failed: " + dataDir);
                showLaunchOverlay(tr("The game could not start. Check that the complete AdvData folder is available, then add it again.",
                        "Game tidak dapat dimulai. Pastikan folder AdvData lengkap tersedia, lalu tambahkan kembali.",
                        "ゲームを起動できません。完全な AdvData フォルダーを確認し、再追加してください。"));
                return;
            }
        }
        notifyUser(dc4SetPaused(false, pauseCheckpoint));
        touches.clear();
        running = true;
        firstFramePending = launchOverlay != null;
        final View pendingOverlay = launchOverlay;
        renderThread = new Thread(() -> {
            Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
            while (running) {
                long started = SystemClock.uptimeMillis();
                Canvas canvas = null;
                SurfaceHolder holder = surface.getHolder();
                try {
                    Runnable command;
                    while ((command = menuCommands.poll()) != null) command.run();
                    Touch touch;
                    while ((touch = touches.poll()) != null) dispatchTouch(touch);
                    if (dc4ConfirmActive()) dc4ConfirmFrame(frame);
                    else if (dc4MenuActive()) dc4MenuFrame(frame);
                    else if (slotMenuOpen) dc4SlotsFrame(frame);
                    else if (backlogOpen) dc4LogFrame(frame);
                    else dc4Render(frame);
                    pollStatus();
                    driveModes(SystemClock.uptimeMillis());
                    canvas = holder.lockCanvas();
                    if (canvas != null) {
                        canvas.drawColor(Color.BLACK);
                        RectF vp = viewport(canvas.getWidth(), canvas.getHeight());
                        canvas.drawBitmap(frame, null, vp, paint);
                        drawOverlays(canvas, vp);
                    }
                } catch (RuntimeException error) {
                    Log.e(TAG, "render loop failed", error);
                    notifyUser("Rendering stopped. See logcat dc4.");
                    running = false;
                } finally {
                    if (canvas != null) {
                        holder.unlockCanvasAndPost(canvas);
                        if (firstFramePending) {
                            firstFramePending = false;
                            runOnUiThread(() -> {
                                if (launchOverlay == pendingOverlay && !initializationFailed) {
                                    gameLayout.removeView(launchOverlay); launchOverlay = null;
                                }
                            });
                        }
                    }
                }
                try {
                    Thread.sleep(Math.max(1, 16 - (SystemClock.uptimeMillis() - started)));
                } catch (InterruptedException ignored) { break; }
            }
        }, "dc4-render");
        renderThread.start();
    }

    private void stopRendering() {
        running = false;
        Thread thread = renderThread;
        if (thread != null) {
            thread.interrupt();
            boolean interrupted = false;
            for (;;) {
                try { thread.join(); break; }
                catch (InterruptedException ignored) { interrupted = true; }
            }
            renderThread = null;
            if (interrupted) Thread.currentThread().interrupt();
        }
        touches.clear();
        menuCommands.clear();
        pendingSaveSlot = -1;
        ++saveRequestToken;
        activePointerId = -1;
        if (initialized) notifyUser(dc4SetPaused(true, pauseCheckpoint));
    }

    // ------------------------------------------------------------------ input

    /** Routes one queued touch to the open overlay or to the native scene. */
    private void dispatchTouch(Touch touch) {
        if(dc4ConfirmActive()){
            handleOriginalAction(dc4ConfirmTouch(touch.action,touch.x,touch.y));return;
        }
        if (dc4MenuActive()) {
            handleOriginalAction(dc4MenuTouch(touch.action, touch.x, touch.y));
            return;
        }
        if (backlogOpen) { backlogTouch(touch); return; }
        if (slotMenuOpen) { slotTouch(touch); return; }
        final String out = dc4Touch(touch.action, touch.x, touch.y);
        if (out == null) return;
        if (out.startsWith(":")) {
            shellAction(out);
        } else if (!out.isEmpty()) {
            notifyUser(out);
        } else if (touch.action == MotionEvent.ACTION_UP && touch.x >= 0) {
            // A plain scene tap cancels auto/skip, like the desktop shell.
            if (!stChoice || stopAutoChoice) autoMode = false;
            if (!stChoice || stopSkipChoice) skipMode = false;
        }
    }

    private void shellAction(String tag) {
        switch (tag) {
            case ":auto":
                autoMode = !stChoice && !autoMode;
                skipMode = false;
                break;
            case ":rapid":
            case ":skip":
                skipMode = !stChoice && !skipMode;
                autoMode = false;
                break;
            case ":backlog":
                openBacklog();
                break;
            case ":qs":
                autoMode=skipMode=false;handleOriginalAction(dc4RequestConfirm("quick-save"));
                break;
            case ":ql":
                autoMode=skipMode=false;handleOriginalAction(dc4RequestConfirm("quick-load"));
                break;
            case ":ds":
                openSlots(false);
                break;
            case ":dl":
                openSlots(true);
                break;
            case ":option":
                closeMenus();
                notifyUser(dc4OpenMenu("config"));
                break;
            case ":aatitle":
                autoMode=skipMode=false;handleOriginalAction(dc4RequestConfirm("title"));
                break;
            case ":n":
            case ":off":
            case ":close":
            case ":clear":
                autoMode = false;
                skipMode = false;
                break;
            default:
                break;
        }
    }

    private void openBacklog() {
        backlogOpen = true;
        dc4LogOpen();
        autoMode = false;
        skipMode = false;
    }

    private void closeMenus() {
        slotMenuOpen = false;
        backlogOpen = false;
        pendingSaveSlot = -1;
        ++saveRequestToken;
    }

    private void backlogTouch(Touch touch) {
        final String out = dc4LogTouch(touch.action, touch.x, touch.y);
        if (out == null || out.isEmpty()) return;
        if (out.equals("close") || out.equals("jump")) closeMenus();
        else if (!out.equals("check") && !out.equals("voice")) notifyUser(out);
    }

    private void slotTouch(Touch touch) {
        if (pendingSaveSlot >= 0) return;
        final String action = dc4SlotsTouch(touch.action, touch.x, touch.y);
        if (action == null || action.isEmpty()) return;
        if (action.startsWith("slot:")) {
            final int slot = Integer.parseInt(action.substring(5));
            if (slot < 0 || slot >= SLOT_COUNT) return;
            persistState(slotMenuLoading, slot);
        } else if (action.equals("load") || action.equals("save")) {
            slotMenuLoading = action.equals("load");
        } else if (action.equals("close")) { closeMenus(); notifyUser(dc4OpenMenu("resume")); }
        else if (action.equals("config")) { closeMenus(); notifyUser(dc4OpenMenu("config")); }
        else if (action.equals("title")) menuCommands.add(() -> { closeMenus(); notifyUser(dc4OpenMenu("title")); });
        else if (action.equals("quit")) runOnUiThread(() -> leaveGame(true));
        else if (action.equals("quick")) persistState(true, -1);
        else if (action.equals("latest") && latestSlot >= 0) persistState(true, latestSlot);
    }

    private void confirmOverwrite(int slot) {
        pendingSaveSlot = slot;
        final int token = ++saveRequestToken;
        runOnUiThread(() -> {
            if (!resumed) return;
            saveDialog = dialog()
                    .setTitle(tr("Replace save?", "Ganti simpanan?", "セーブを上書きしますか？"))
                    .setMessage(tr("Replace the saved game in slot ", "Ganti simpanan pada slot ", "次のスロットを上書きします：") + (slot + 1) + "?")
                    .setPositiveButton(tr("Save", "Simpan", "セーブ"), (dialog, which) -> menuCommands.add(() -> {
                        if (saveRequestToken == token && slotMenuOpen && !slotMenuLoading &&
                                pendingSaveSlot == slot) {
                            pendingSaveSlot = -1;
                            persistState(false, slot);
                        }
                    }))
                    .setNegativeButton(tr("Cancel", "Batal", "キャンセル"), (dialog, which) -> cancelOverwrite(token))
                    .setOnCancelListener(dialog -> cancelOverwrite(token)).create();
            saveDialog.show();
        });
    }

    private void cancelOverwrite(int token) {
        menuCommands.add(() -> {
            if (saveRequestToken == token) pendingSaveSlot = -1;
        });
    }

    private void openSlots(boolean loading) {
        autoMode = false;
        skipMode = false;
        backlogOpen = false;
        slotMenuLoading = loading;
        pendingSaveSlot = -1;
        ++saveRequestToken;
        refreshSlots();
        notifyUser(dc4SlotsOpen(loading, latestSlot >= 0,
                new File(savesDir(), "quicksave.dc4sav").isFile()));
        slotMenuOpen = true;
    }

    // ------------------------------------------------------------ save / load

    private void persistState(boolean load, int slot) {
        if (slot < -1 || slot >= SLOT_COUNT) return;
        autoMode = false;
        skipMode = false;
        final File dir = savesDir();
        final File file = slot < 0 ? new File(dir, "quicksave.dc4sav")
                : new File(dir, "slot" + (slot + 1) + ".dc4sav");
        final String what = slot < 0 ? (load ? tr("Quick load", "Muat cepat", "クイックロード") : tr("Quick save", "Simpan cepat", "クイックセーブ"))
                : (load ? tr("Load", "Muat", "ロード") : tr("Save", "Simpan", "セーブ")) + " · " + (slot + 1);
        if (load) {
            if (!file.isFile()) { notifyUser(what + tr(": empty slot", ": slot kosong", "：空きスロット")); return; }
            final String error = dc4Load(file.getAbsolutePath());
            if (!error.isEmpty()) { notifyUser(error); return; }
            dc4LogClear();
            lastLineSerial = -1;
            closeMenus();
            notifyUser(what + tr(" restored", " dipulihkan", "を読み込みました"));
        } else {
            final String error = dc4Save(file.getAbsolutePath());
            if (!error.isEmpty()) { notifyUser(error); return; }
            writePreview(file);
            if (slot >= 0 && slotMenuOpen) {
                refreshSlots();
                notifyUser(dc4SlotsOpen(false, latestSlot >= 0,
                        new File(dir, "quicksave.dc4sav").isFile()));
            } else closeMenus();
            notifyUser(what + tr(" written", " tersimpan", "を保存しました"));
        }
    }

    /** Optional 128x72 thumbnail + dialogue excerpt sidecars for the picker. */
    private void writePreview(File save) {
        try {
            if (previewBitmap == null)
                previewBitmap = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
            dc4RenderPreview(previewBitmap);
            final Bitmap thumb = Bitmap.createScaledBitmap(previewBitmap, 128, 72, true);
            try (FileOutputStream out = new FileOutputStream(save.getAbsolutePath() + ".png")) {
                thumb.compress(Bitmap.CompressFormat.PNG, 90, out);
            } finally { thumb.recycle(); }
            String speaker = dc4Speaker();
            String text = dc4CurrentLine().replace('\n', ' ').replace('\r', ' ');
            if (text.length() > 200) text = text.substring(0, 200);
            final String excerpt = speaker.isEmpty() ? text : speaker + ": " + text;
            try (FileOutputStream out = new FileOutputStream(save.getAbsolutePath() + ".txt")) {
                out.write(excerpt.getBytes(StandardCharsets.UTF_8));
            }
        } catch (RuntimeException | IOException error) {
            Log.w(TAG, "save preview unavailable", error);
        }
    }

    private void refreshSlots() {
        if (dateFormat == null) dateFormat = new SimpleDateFormat("yyyy MM/dd HH:mm:ss", Locale.US);
        final File dir = savesDir();
        latestSlot = -1;
        long newest = Long.MIN_VALUE;
        for (int i = 0; i < SLOT_COUNT; ++i) {
            final File file = new File(dir, "slot" + (i + 1) + ".dc4sav");
            final boolean occupied = file.isFile();
            String timestamp = "", excerpt = "";
            Bitmap thumbnail = null;
            if (occupied) {
                final long modified = file.lastModified();
                if (modified > newest) { newest = modified; latestSlot = i; }
                timestamp = dateFormat.format(new Date(modified));
                excerpt = readSmallFile(file.getAbsolutePath() + ".txt");
                final String image = file.getAbsolutePath() + ".png";
                BitmapFactory.Options options = new BitmapFactory.Options();
                options.inJustDecodeBounds = true;
                BitmapFactory.decodeFile(image, options);
                if (options.outWidth == 128 && options.outHeight == 72 && new File(image).length() <= 100000) {
                    options.inJustDecodeBounds = false;
                    options.inPreferredConfig = Bitmap.Config.ARGB_8888;
                    thumbnail = BitmapFactory.decodeFile(image, options);
                }
            }
            dc4SlotInfo(i, occupied, timestamp, excerpt, thumbnail);
            if (thumbnail != null) thumbnail.recycle();
        }
    }

    private static String readSmallFile(String path) {
        try (InputStream in = new FileInputStream(path)) {
            final byte[] data = new byte[512];
            final int n = in.read(data);
            return n <= 0 ? "" : new String(data, 0, n, StandardCharsets.UTF_8);
        } catch (IOException error) {
            return "";
        }
    }

    // -------------------------------------------------------- backlog + modes

    private void pollStatus() {
        String playback = dc4PlaybackSettings();
        if (playback != null) {
            String[] settings = playback.split(";", 5);
            if (settings.length == 5) {
                try { autoDelayMs = Math.max(0, Math.min(30000, Integer.parseInt(settings[0]))); } catch (NumberFormatException ignored) { }
                waitVoice = "1".equals(settings[1]); stopAutoChoice = "1".equals(settings[2]);
                stopSkipChoice = "1".equals(settings[3]); skipAllowed = "1".equals(settings[4]);
            }
        }
        final String status = dc4Status();
        if (status == null || status.isEmpty()) return;
        final String[] parts = status.split(";", 7);
        if (parts.length < 7) return;
        try {
            stLine = Integer.parseInt(parts[0]);
        } catch (NumberFormatException error) {
            return;
        }
        stChoice = "1".equals(parts[1]);
        stVoice = "1".equals(parts[2]);
        stAnim = "1".equals(parts[3]);
        stRevealed = "1".equals(parts[4]);
        stVisible = "1".equals(parts[5]);
        stEnded = "1".equals(parts[6]);
        if (stLine != lastLineSerial) {
            lastLineSerial = stLine;
            wasRevealed = false;
        }
        if (stRevealed && !wasRevealed) revealedAt = SystemClock.uptimeMillis();
        wasRevealed = stRevealed;
    }

    /** Auto/skip drivers, mirroring the desktop shell's timings. */
    private void driveModes(long now) {
        if (!autoMode && !skipMode) return;
        if (skipMode && !skipAllowed) skipMode = false;
        if (backlogOpen || slotMenuOpen || dc4MenuActive() || dc4ConfirmActive()) return;
        if (stEnded) {
            if (!wasEnded) {
                autoMode = false;
                skipMode = false;
                notifyUser(tr("End of scenario", "Akhir skenario", "シナリオ終了"));
            }
            wasEnded = true;
            return;
        }
        wasEnded = false;
        if (stChoice) {
            if (!choiceNotified) {
                if (stopAutoChoice) autoMode = false;
                if (stopSkipChoice) skipMode = false;
                notifyUser(tr("Choose an option", "Pilih opsi", "選択肢を選んでください"));
                choiceNotified = true;
            }
            return;
        }
        choiceNotified = false;
        if (skipMode) {
            if (now - lastSkipTick >= 100) {
                lastSkipTick = now;
                if (!dc4SkipAdvance()) {
                    autoMode = false;
                    skipMode = false;
                    notifyUser("Scene stopped (see logcat dc4)");
                }
            }
        } else if (autoMode && stRevealed && (!waitVoice || !stVoice) && !stAnim
                && now - revealedAt >= autoDelayMs) {
            if (!dc4Advance()) {
                autoMode = false;
                notifyUser("Scene stopped (see logcat dc4)");
            }
            // Re-armed when the next line's reveal completes.
            revealedAt = Long.MAX_VALUE / 2;
        }
    }

    // ---------------------------------------------------------------- drawing

    private void drawOverlays(Canvas canvas, RectF vp) {
        if (!autoMode && !skipMode) return;
        ensurePaints();
        canvas.save();
        canvas.translate(vp.left, vp.top);
        final float scale = vp.width() / (float) W;
        canvas.scale(scale, scale);
        drawModeBadge(canvas);
        canvas.restore();
    }

    private void ensurePaints() {
        if (badgePaint != null) return;
        badgePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        badgePaint.setColor(Color.WHITE);
        badgePaint.setTextSize(20);
        badgePaint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        badgeBgPaint = new Paint();
        badgeBgPaint.setColor(0x90101820);
    }

    private void drawModeBadge(Canvas canvas) {
        if (!autoMode && !skipMode) return;
        final String label = skipMode ? "SKIP" : "AUTO";
        final float width = badgePaint.measureText(label) + 28;
        canvas.drawRect(16, 14, 16 + width, 50, badgeBgPaint);
        canvas.drawText(label, 30, 39, badgePaint);
    }

    // ------------------------------------------------------- Android library

    private TextView text(String label, int size, int color) {
        return ArisaUi.text(this, label, size, color);
    }
    private Button button(String label, Runnable action) {
        return ArisaUi.button(this, label, false, () -> { if (!ioBusy) action.run(); });
    }
    private Button primary(String label, Runnable action) {
        return ArisaUi.button(this, label, true, () -> { if (!ioBusy) action.run(); });
    }
    private AlertDialog.Builder dialog() { return new ArisaUi.DialogBuilder(this); }
    private String settingsTitle() { return tr("Arisa32 · adv32 Settings", "Pengaturan Arisa32 (adv32)", "Arisa32 (adv32) 設定"); }
    private LinearLayout panel() {
        LinearLayout card = ArisaUi.column(this); card.setBackground(ArisaUi.surface(this, ArisaUi.PANEL, 10));
        card.setPadding(dp(18), dp(16), dp(18), dp(16)); return card;
    }
    private void spaced(LinearLayout row, View child, float weight) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, -2, weight);
        if (row.getChildCount() > 0) p.leftMargin = dp(8); row.addView(child, p);
    }
    private void section(LinearLayout body, String label) {
        ArisaUi.gap(body, 14); body.addView(ArisaUi.eyebrow(this, label)); ArisaUi.gap(body, 6);
    }
    private void setting(LinearLayout body, String label, String detail, Runnable action) {
        LinearLayout row = new LinearLayout(this); row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(12), dp(12), dp(12)); row.setMinimumHeight(dp(56));
        row.setBackground(ArisaUi.ripple(this, 0x441c2738, 8));
        LinearLayout words = ArisaUi.column(this); words.addView(text(label, 15, TEXT));
        if (detail != null && !detail.isEmpty()) { ArisaUi.gap(words, 4); words.addView(text(detail, 12, MUTED)); }
        row.addView(words, new LinearLayout.LayoutParams(0, -2, 1));
        TextView arrow = text("›", 22, ArisaUi.CYAN); arrow.setPadding(dp(12), 0, 0, 0);
        arrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO); row.addView(arrow);
        row.setFocusable(true); row.setContentDescription(label + (detail == null ? "" : ". " + detail));
        words.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
        row.setOnClickListener(v -> { if (!ioBusy) { if (settingsDialog != null) settingsDialog.dismiss(); action.run(); } });
        body.addView(row, new LinearLayout.LayoutParams(-1, -2)); ArisaUi.gap(body, 6);
    }
    private void sheet(String title, LinearLayout content) {
        content.setPadding(dp(20), 0, dp(20), dp(12));
        ArisaUi.LimitedScroll scroll = new ArisaUi.LimitedScroll(this); scroll.addView(content);
        settingsDialog = dialog().setTitle(title).setView(scroll)
                .setNegativeButton(tr("Close", "Tutup", "閉じる"), null).show();
    }
    private void showLibrary() {
        if (destroyed || library == null) return;
        inLibrary = true;
        FrameLayout root = ArisaUi.backdrop(this);
        LinearLayout shell = ArisaUi.column(this); shell.setPadding(dp(24), dp(14), dp(24), dp(12));
        root.addView(shell, new FrameLayout.LayoutParams(-1, -1));
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            shell.setPadding(dp(24) + insets.getSystemWindowInsetLeft(), dp(14) + insets.getSystemWindowInsetTop(),
                    dp(24) + insets.getSystemWindowInsetRight(), dp(12) + insets.getSystemWindowInsetBottom()); return insets;
        });
        LinearLayout header = new LinearLayout(this); header.setGravity(Gravity.CENTER_VERTICAL);
        header.addView(ArisaUi.logo(this), new LinearLayout.LayoutParams(dp(40), dp(40)));
        LinearLayout brand = ArisaUi.column(this); brand.setPadding(dp(12), 0, 0, 0);
        TextView wordmark = text("Arisa32", 22, TEXT); wordmark.setTypeface(Typeface.create("sans-serif-black", Typeface.BOLD)); brand.addView(wordmark);
        ArisaUi.gap(brand, 2); brand.addView(ArisaUi.eyebrow(this, "ADV32 · CIRCUS ENGINE RUNTIME"));
        header.addView(brand, new LinearLayout.LayoutParams(0, -2, 1));
        header.addView(button(tr("Settings", "Pengaturan", "設定"), this::showAppSettings)); shell.addView(header);
        ArisaUi.gap(shell, 16);
        ScrollView scroll = new ScrollView(this); scroll.setClipToPadding(false); scroll.setPadding(0, 0, dp(4), dp(8));
        scroll.setOverScrollMode(View.OVER_SCROLL_IF_CONTENT_SCROLLS); scroll.setVerticalScrollBarEnabled(false);
        LinearLayout body = ArisaUi.column(this); scroll.addView(body);
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(-1, 0, 1); shell.addView(scroll, scrollParams);
        shell.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob) -> {
            int available = r-l-shell.getPaddingLeft()-shell.getPaddingRight();
            int width = available < dp(600) ? available : Math.min(dp(640), (int)(available * .64f));
            if (scroll.getLayoutParams().width != width) { scroll.getLayoutParams().width = width; scroll.requestLayout(); }
        });
        LinearLayout heading = new LinearLayout(this); heading.setGravity(Gravity.CENTER_VERTICAL);
        TextView title = text(tr("Game Library", "Daftar Game", "ゲーム一覧"), 24, TEXT);
        title.setTypeface(Typeface.create("sans-serif-bold", Typeface.BOLD)); heading.addView(title, new LinearLayout.LayoutParams(0, -2, 1));
        if (!library.entries.isEmpty()) heading.addView(button(tr("+ Add game", "+ Tambah game", "＋ 追加"), this::chooseAddMethod));
        body.addView(heading); ArisaUi.gap(body, 4);
        body.addView(text(tr("CIRCUS Visual Novel Runtime & Script VM", "Runtime Skrip & Visual Novel CIRCUS", "CIRCUS ビジュアルノベル スクリプト実行環境"), 12, MUTED));
        ArisaUi.gap(body, 14);
        if (library.entries.isEmpty()) {
            LinearLayout empty = panel();
            empty.addView(ArisaUi.eyebrow(this, tr("NO GAMES FOUND", "DATA GAME BELUM ADA", "ゲームデータが見つかりません")));
            ArisaUi.gap(empty, 8);
            empty.addView(text(tr("No AdvData directory detected.", "Folder AdvData belum ditambahkan.", "AdvData フォルダーが設定されていません。"), 18, TEXT));
            ArisaUi.gap(empty, 6);
            empty.addView(text(tr("Select an installed game folder containing AdvData or MES scripts to begin.",
                    "Pilih folder game yang memuat direktori AdvData atau skrip MES untuk mulai bermain.",
                    "AdvData または MES スクリプトを含むフォルダーを選択してください。"), 13, MUTED));
            ArisaUi.gap(empty, 14);
            empty.addView(primary(tr("+ Select Game Folder", "+ Pilih Folder Game", "＋ ゲームフォルダーを選択"), this::chooseAddMethod));
            body.addView(empty);
        }
        for (GameLibrary.Entry game : library.entries) {
            LinearLayout card = panel();
            LinearLayout meta = new LinearLayout(this); meta.setGravity(Gravity.CENTER_VERTICAL);
            meta.addView(ArisaUi.eyebrow(this, "ADV32 · CIRCUS"), new LinearLayout.LayoutParams(0, -2, 1));
            meta.addView(ArisaUi.badge(this, game.language.equals("jp") ? "JP (ORIGINAL)" : game.language.equals("en") ? "EN PATCH" : "ID PATCH")); card.addView(meta);
            ArisaUi.gap(card, 8);
            TextView name = text(game.title, 20, TEXT); name.setTypeface(Typeface.create("sans-serif-bold", 0));
            name.setMaxLines(2); name.setEllipsize(android.text.TextUtils.TruncateAt.END); card.addView(name);
            ArisaUi.gap(card, 4);
            card.addView(text(game.path, 11, MUTED));
            ArisaUi.gap(card, 14);
            LinearLayout actions = new LinearLayout(this);
            spaced(actions, primary(tr("LAUNCH", "JALANKAN", "起動"), () -> launchGame(game)), 1);
            spaced(actions, button(tr("MANAGE", "KELOLA", "管理"), () -> showGameSettings(game)), 1);
            card.addView(actions);
            body.addView(card);
            ArisaUi.gap(body, 10);
        }
        ArisaUi.gap(shell, 6);
        TextView footer = text("Arisa32 · adv32 Runtime v0.2.0 (CIRCUS Engine)", 11, MUTED); shell.addView(footer);
        setContentView(root); root.requestApplyInsets();
    }
    private void showAppSettings() {
        LinearLayout body = ArisaUi.column(this);
        section(body, tr("PREFERENCES", "PREFERENSI", "環境設定"));
        setting(body, tr("App language", "Bahasa aplikasi", "アプリ言語"), uiLanguage.equals("jp") ? "日本語" : uiLanguage.equals("id") ? "Bahasa Indonesia" : "English", this::chooseUiLanguage);
        section(body, tr("GAME STORAGE", "PENYIMPANAN GAME", "ゲームストレージ"));
        setting(body, tr("Add game", "Tambah game", "ゲーム追加"), tr("Select AdvData or game directory", "Pilih folder AdvData atau direktori game", "AdvData またはゲームディレクトリを選択"), this::chooseAddMethod);
        setting(body, tr("Storage access", "Akses penyimpanan", "ストレージ権限"), tr("Required for reading external AdvData folders", "Diperlukan untuk membaca folder AdvData eksternal", "外部 AdvData 読み取りに必要"), () -> {
            if (ensureStorageAccess()) notifyUser(tr("Storage access is already enabled.", "Akses penyimpanan sudah aktif.", "ストレージへのアクセスは許可されています。"));
        });
        section(body, tr("ENGINE SPECIFICATION", "SPESIFIKASI ENGINE", "エンジン仕様"));
        body.addView(text("Arisa32 · adv32 v0.2.0", 16, TEXT)); ArisaUi.gap(body, 6);
        body.addView(text(tr("CIRCUS Visual Novel Engine runtime for Android. Executes native MES scripts, CRM/CRX graphics, and XPCM audio.",
                "Runtime engine visual novel CIRCUS untuk Android. Menjalankan skrip MES asli, grafis CRM/CRX, dan audio XPCM.",
                "Android向け CIRCUS adv32 ビジュアルノベルエンジン。MESスクリプト、CRM/CRX画像、XPCM音声をネイティブ再生。"), 13, MUTED));
        sheet(settingsTitle(), body);
    }
    private String contentName(String code) {
        return code.equals("en") ? "English" : code.equals("id") ? "Bahasa Indonesia" : tr("Original (Japanese)", "Asli (Jepang)", "オリジナル（日本語）");
    }
    private void chooseUiLanguage() {
        settingsDialog = dialog().setTitle(tr("App language", "Bahasa aplikasi", "アプリ言語"))
                .setSingleChoiceItems(new String[]{"English", "Bahasa Indonesia", "日本語"}, uiLanguage.equals("id") ? 1 : uiLanguage.equals("jp") ? 2 : 0, (d, which) -> {
                    d.dismiss();
                    uiLanguage = new String[]{"en", "id", "jp"}[which];
                    preferences.edit().putString("uiLanguage", uiLanguage).apply();
                    if (inLibrary) showLibrary(); else if (bubble != null) bubble.setContentDescription(settingsTitle());
                }).setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void chooseAddMethod() {
        LinearLayout body = ArisaUi.column(this);
        body.addView(text(tr("Select AdvData or the folder containing it.", "Pilih AdvData atau folder yang memuatnya.", "AdvData またはそれを含むフォルダーを選択してください。"), 13, MUTED));
        ArisaUi.gap(body, 14);
        setting(body, tr("Choose folder", "Pilih folder", "フォルダーを選択"),
                tr("Copy into app storage. Requires available internal storage.", "Salin ke penyimpanan aplikasi. Memerlukan ruang penyimpanan yang cukup.", "アプリ領域にコピーします。空き容量が必要です。"), () -> pickTree(PICK_GAME));
        setting(body, tr("Link existing folder", "Tautkan folder yang ada", "既存のフォルダーを登録"),
                tr("Direct path to installed AdvData directory. Must be readable by the engine.", "Jalur langsung ke folder AdvData game. Harus dapat dibaca oleh engine.", "端末上の AdvData フォルダーへの直接パス。"), this::showPathEntry);
        sheet(tr("Add a game", "Tambah game", "ゲームを追加"), body);
    }
    private void showPathEntry() {
        EditText path = ArisaUi.input(this, tr("Game folder path", "Jalur folder game", "ゲームフォルダーのパス")); path.setText("/storage/emulated/0/DC4/AdvData");
        path.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        FrameLayout box = new FrameLayout(this);
        box.setPadding(dp(20), dp(10), dp(20), dp(8));
        box.addView(path, new FrameLayout.LayoutParams(-1, -2));
        settingsDialog = dialog().setTitle(tr("Game folder path", "Jalur folder game", "ゲームフォルダーのパス"))
                .setMessage(tr("Specify filesystem path to AdvData. Folder picker is recommended and works without broad storage permissions.",
                        "Tentukan jalur sistem berkas ke AdvData. Pemilih folder disarankan karena tidak memerlukan izin akses semua berkas.",
                        "AdvData フォルダーへのパスを指定してください。フォルダー選択機能なら全ファイルアクセス権限は不要です。"))
                .setView(box).setPositiveButton(tr("Add", "Tambah", "追加"), (d, w) -> registerPath(new File(path.getText().toString().trim()), null, null, false))
                .setNeutralButton(tr("Storage access", "Akses penyimpanan", "ストレージ権限"), (d, w) -> ensureStorageAccess())
                .setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void persistLibrary() {
        try { library.save(); } catch (IOException error) { notifyUser(error.getMessage()); }
    }
    private void launchGame(GameLibrary.Entry game) {
        if (ioBusy) return;
        if (probeDir(new File(game.path)) == null) {
            settingsDialog = dialog().setTitle(tr("Reconnect your game", "Hubungkan kembali game", "ゲームを再登録"))
                    .setMessage(tr("Game files are unavailable. Add the folder again to reconnect it. Your saves are kept.", "Berkas game tidak tersedia. Tambahkan ulang folder untuk menghubungkannya. Simpanan Anda tetap tersimpan.", "ゲームデータを読み取れません。フォルダーを再追加してください。セーブは保持されます。"))
                    .setPositiveButton(tr("Add folder", "Tambah folder", "フォルダーを追加"), (d,w) -> chooseAddMethod())
                    .setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show(); return;
        }
        if (!game.overlay().isEmpty() && !new File(game.overlay(), "MES").isDirectory()) {
            notifyUser(tr("The selected language pack is unavailable.", "Paket bahasa yang dipilih tidak tersedia.", "選択された翻訳パックを読み取れません。")); return;
        }
        if (initialized || renderThread != null) leaveGame(false);
        selected = game; inLibrary = false; initializationFailed = false; surfaceReady = false;
        lastLineSerial = -1; wasEnded = wasRevealed = choiceNotified = false; latestSlot = -1;
        createGameSurface();
    }
    private void leaveGame(boolean show) {
        stopRendering();
        if (initialized) dc4Quit();
        initialized = false; initializationFailed = false;
        autoMode = skipMode = backlogOpen = slotMenuOpen = false;
        surfaceReady = false; inLibrary = true; launchOverlay = null;
        if (show) showLibrary();
    }
    private void showLaunchOverlay(String error) {
        if (gameLayout == null || inLibrary || destroyed) return;
        if (launchOverlay != null) gameLayout.removeView(launchOverlay);
        FrameLayout overlay = ArisaUi.backdrop(this); overlay.setClickable(true); overlay.setFocusable(true);
        ScrollView scroll = new ScrollView(this); LinearLayout card = panel();
        card.addView(ArisaUi.eyebrow(this, "ADV32 / CIRCUS ENGINE")); ArisaUi.gap(card, 8);
        card.addView(text(error == null ? selected.title : tr("Unable to start", "Tidak dapat memulai", "起動できません"), 22, TEXT)); ArisaUi.gap(card, 8);
        TextView message = text(error == null ? tr("Starting engine & loading scripts…", "Memulai engine & memuat skrip…", "エンジン起動・スクリプト読込中…") : error, 14, MUTED);
        message.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE); card.addView(message);
        if (error != null) { ArisaUi.gap(card, 14); card.addView(primary(tr("Return to library", "Kembali ke koleksi", "ライブラリに戻る"), () -> leaveGame(true))); }
        scroll.addView(card); FrameLayout.LayoutParams p = new FrameLayout.LayoutParams(Math.min(dp(460), getResources().getDisplayMetrics().widthPixels-dp(64)), -2, Gravity.CENTER);
        p.topMargin = p.bottomMargin = dp(20); overlay.addView(scroll, p);
        gameLayout.addView(overlay, new FrameLayout.LayoutParams(-1, -1)); launchOverlay = overlay;
    }
    private void addSettingsBubble() {
        bubble = text("⋮", 25, ArisaUi.CYAN); bubble.setGravity(Gravity.CENTER); bubble.setPadding(0, 0, 0, 0);
        bubble.setBackground(ArisaUi.ripple(this, 0xe514243b, 18)); bubble.setElevation(dp(6)); bubble.setFocusable(true);
        bubble.setContentDescription(settingsTitle());
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(dp(48), dp(48)); gameLayout.addView(bubble, params);
        bubble.setX(dp(12)); bubble.setY(dp(12));
        final float[] drag = new float[4]; final boolean[] moved = {false};
        bubble.setOnTouchListener((v, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    drag[0] = event.getRawX(); drag[1] = event.getRawY(); drag[2] = v.getX(); drag[3] = v.getY(); moved[0] = false; return true;
                case MotionEvent.ACTION_MOVE:
                    float dx = event.getRawX() - drag[0], dy = event.getRawY() - drag[1];
                    if (Math.abs(dx) + Math.abs(dy) > dp(8)) moved[0] = true;
                    v.setX(drag[2] + dx); v.setY(drag[3] + dy); clampBubble(); return true;
                case MotionEvent.ACTION_UP:
                    if (!moved[0]) v.performClick();
                    else preferences.edit().putFloat("bubbleX", v.getX() / Math.max(1, gameLayout.getWidth()))
                            .putFloat("bubbleY", v.getY() / Math.max(1, gameLayout.getHeight())).apply();
                    return true;
                case MotionEvent.ACTION_CANCEL: return true;
                default: return true;
            }
        });
        bubble.setOnClickListener(v -> { if (!ioBusy) showExtras(); });
        gameLayout.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> clampBubble());
        gameLayout.post(() -> {
            bubble.setX(preferences.getFloat("bubbleX", .015f) * gameLayout.getWidth());
            bubble.setY(preferences.getFloat("bubbleY", .025f) * gameLayout.getHeight()); clampBubble();
        });
    }
    private void clampBubble() {
        if (bubble == null || gameLayout == null) return;
        bubble.setX(Math.max(dp(4), Math.min(bubble.getX(), gameLayout.getWidth() - bubble.getWidth() - dp(4))));
        // The game's message-window buttons occupy the lower half of the viewport.
        bubble.setY(Math.max(dp(4), Math.min(bubble.getY(), Math.min(gameLayout.getHeight() - bubble.getHeight() - dp(4), gameLayout.getHeight() * .4f))));
    }
    private void showExtras() {
        menuCommands.add(() -> { autoMode = false; skipMode = false; });
        LinearLayout body = ArisaUi.column(this);
        section(body, tr("NOW PLAYING", "SEDANG DIMAINKAN", "プレイ中"));
        setting(body, tr("Game settings", "Pengaturan game", "ゲーム設定"), tr("Audio, text speed and playback", "Audio, kecepatan teks dan pemutaran", "音声・文字速度・再生設定"),
                () -> menuCommands.add(() -> { closeMenus(); notifyUser(dc4OpenMenu("config")); }));
        setting(body, tr("Manage game", "Kelola game", "ゲーム管理"), selected.title + " · " + contentName(selected.language), () -> showGameSettings(selected));
        setting(body, tr("App language", "Bahasa aplikasi", "アプリの言語"), null, this::chooseUiLanguage);
        section(body, tr("NAVIGATION", "NAVIGASI", "移動"));
        setting(body, tr("Title screen", "Layar judul", "タイトル画面"), null, this::confirmTitle);
        setting(body, tr("Return to library", "Kembali ke koleksi", "ライブラリに戻る"), null, this::confirmLibrary);
        sheet(settingsTitle(), body);
    }
    private void confirmTitle() {
        settingsDialog = dialog().setTitle(tr("Return to title?", "Kembali ke judul?", "タイトルに戻りますか？"))
                .setMessage(tr("Save your progress first. Unsaved progress may be lost.", "Simpan progres terlebih dahulu. Progres yang belum disimpan bisa hilang.", "先に進行状況をセーブしてください。未保存の進行状況は失われる場合があります。"))
                .setPositiveButton(tr("Title", "Judul", "タイトル"), (d,w) -> menuCommands.add(() -> { closeMenus(); notifyUser(dc4OpenMenu("title")); }))
                .setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void confirmLibrary() {
        settingsDialog = dialog().setTitle(tr("Return to library?", "Kembali ke koleksi?", "ライブラリに戻りますか？"))
                .setMessage(tr("Save your progress first. Unsaved progress will be lost.", "Simpan progres terlebih dahulu. Progres yang belum disimpan akan hilang.", "先に進行状況をセーブしてください。未保存の進行状況は失われます。"))
                .setPositiveButton(tr("Library", "Koleksi", "ライブラリ"), (d,w) -> leaveGame(true))
                .setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void handleOriginalAction(String action) {
        if (action == null || action.isEmpty()) return;
        if (action.startsWith(":")) action = action.substring(1);
        switch (action) {
            case "save": openSlots(false); break;
            case "load": openSlots(true); break;
            case "backlog": openBacklog(); break;
            case "start": closeMenus(); lastLineSerial = -1; break;
            case "jump": closeMenus(); lastLineSerial = -1; break;
            case "quick-save-confirmed": persistState(false,-1); break;
            case "quick-load-confirmed": persistState(true,-1); break;
            case "title-confirmed": closeMenus(); notifyUser(dc4OpenMenu("title")); break;
            case "quit-confirmed": runOnUiThread(() -> leaveGame(true)); break;
            case "quit": runOnUiThread(() -> leaveGame(true)); break;
            case "close": closeMenus(); break;
            default: notifyUser(action); break;
        }
    }
    private void showGameSettings(GameLibrary.Entry game) {
        settingsDialog = dialog().setTitle(game.title)
                .setItems(new String[]{tr("Game language: ", "Bahasa game: ", "ゲーム言語：") + contentName(game.language),
                        tr("Import language pack", "Impor paket bahasa", "翻訳パックを追加"),
                        tr("Choose dialogue font", "Pilih font dialog", "台詞フォントを選択"),
                        tr("Import font (.ttf / .otf / .ttc)", "Impor font (.ttf / .otf / .ttc)", "フォントを追加（.ttf / .otf / .ttc）"),
                        tr("Export saves", "Ekspor simpanan", "セーブをエクスポート"),
                        tr("Import saves", "Impor simpanan", "セーブをインポート"),
                        tr("Rename game", "Ubah nama game", "ゲーム名を変更"),
                        tr("Remove from library", "Hapus dari koleksi", "ライブラリから削除")}, (d, which) -> {
                    switch (which) {
                        case 0: chooseContent(game); break;
                        case 1: chooseOverlayLanguage(game); break;
                        case 2: chooseFont(game); break;
                        case 3: documentGame = game; pickDocument(PICK_FONT, "*/*", null); break;
                        case 4: documentGame = game; documentLanguage = game.language;
                            pickDocument(EXPORT_SAVES, "application/zip", "Arisa32-" + game.language + "-saves.zip"); break;
                        case 5: confirmImport(game); break;
                        case 6: renameGame(game); break;
                        case 7: removeGame(game); break;
                    }
                }).show();
    }
    private void chooseContent(GameLibrary.Entry game) {
        ArrayList<String> codes = new ArrayList<>(); codes.add("jp");
        if (!game.english.isEmpty()) codes.add("en"); if (!game.indonesian.isEmpty()) codes.add("id");
        String[] labels = new String[codes.size()]; for (int i=0;i<labels.length;i++) labels[i] = contentName(codes.get(i));
        settingsDialog = dialog().setTitle(tr("Game content language", "Bahasa konten game", "ゲーム内容の言語"))
                .setMessage(tr("Import a translation pack to add English or Indonesian. Changing the language returns to the library; save first. Each language has its own saves.",
                        "Impor paket terjemahan untuk menambah bahasa Inggris atau Indonesia. Mengubah bahasa kembali ke koleksi; simpan dulu. Tiap bahasa memiliki simpanan terpisah.",
                        "英語・インドネシア語には翻訳パックが必要です。言語変更でライブラリに戻ります。先にセーブしてください。セーブは言語ごとに分かれます。"))
                .setPositiveButton(tr("Choose", "Pilih", "選択"), (d,w) -> {
                    settingsDialog = dialog().setTitle(tr("Game language", "Bahasa game", "ゲーム言語"))
                            .setSingleChoiceItems(labels, codes.indexOf(game.language), (dialog, which) -> {
                                String code = codes.get(which); dialog.dismiss();
                                if (!code.equals(game.language)) { if (!inLibrary) leaveGame(true); game.language = code; persistLibrary(); showLibrary(); }
                            }).setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
                }).setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void chooseOverlayLanguage(GameLibrary.Entry game) {
        if (!inLibrary) {
            settingsDialog = dialog().setTitle(tr("Import language pack", "Impor paket bahasa", "翻訳パックを追加"))
                    .setMessage(tr("Importing a pack returns to the library. Save your progress before continuing.",
                            "Mengimpor paket akan kembali ke koleksi. Simpan progres sebelum melanjutkan.", "パックの追加ではライブラリに戻ります。続行する前に進行状況をセーブしてください。"))
                    .setPositiveButton(tr("Continue", "Lanjutkan", "続行"), (d,w) -> { leaveGame(true); chooseOverlayLanguage(game); })
                    .setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
            return;
        }
        settingsDialog = dialog().setTitle(tr("Translation pack language", "Bahasa paket terjemahan", "翻訳パックの言語"))
                .setItems(new String[]{"English", "Bahasa Indonesia"}, (d, which) -> {
                    documentGame = game; pendingOverlayLanguage = which == 0 ? "en" : "id";
                    settingsDialog = dialog().setTitle(tr("Pack text encoding", "Enkode teks paket", "パックの文字コード"))
                            .setItems(new String[]{"Windows-31J / CP932", "UTF-8"}, (dialog, encoding) -> {
                                documentLanguage = encoding == 0 ? "windows-31j" : "UTF-8"; pickTree(PICK_OVERLAY);
                            }).show();
                }).show();
    }
    private void chooseFont(GameLibrary.Entry game) {
        File root = new File(getFilesDir(), "fonts"); File[] files = root.listFiles();
        ArrayList<File> fonts = new ArrayList<>(); ArrayList<String> labels = new ArrayList<>(); labels.add(tr("System default", "Bawaan sistem", "システム標準"));
        if (files != null) for (File file : files) if (file.isFile()) { fonts.add(file); labels.add(file.getName()); }
        int checked = 0; for (int i=0;i<fonts.size();i++) if (fonts.get(i).getAbsolutePath().equals(game.font)) checked=i+1;
        settingsDialog = dialog().setTitle(tr("Dialogue font (applies on next launch)", "Font dialog (berlaku saat game dibuka ulang)", "台詞フォント（次回起動時に適用）"))
                .setSingleChoiceItems(labels.toArray(new String[0]), checked, (d,which) -> {
                    game.font = which == 0 ? "" : fonts.get(which - 1).getAbsolutePath(); persistLibrary(); d.dismiss();
                    notifyUser(tr("Font saved. Reopen the game to apply it.", "Font tersimpan. Buka ulang game untuk menerapkannya.", "フォントを保存しました。ゲームを再起動すると適用されます。"));
                }).setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void renameGame(GameLibrary.Entry game) {
        EditText name = new EditText(this); name.setSingleLine(); name.setText(game.title);
        settingsDialog = dialog().setTitle(tr("Rename game", "Ubah nama game", "ゲーム名を変更")).setView(name)
                .setPositiveButton(tr("Save", "Simpan", "保存"), (d,w) -> { String value = name.getText().toString().trim(); if (!value.isEmpty()) { game.title=value.substring(0,Math.min(100,value.length())); persistLibrary(); if(inLibrary)showLibrary(); } })
                .setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void removeGame(GameLibrary.Entry game) {
        settingsDialog = dialog().setTitle(tr("Remove this game?", "Hapus game ini?", "ゲームを削除しますか？"))
                .setMessage(tr("The library entry is removed. Game files and saves are kept; adding the same data reconnects them.",
                        "Entri koleksi dihapus. Berkas game dan simpanan tetap tersimpan; tambahkan data yang sama untuk menghubungkannya kembali.",
                        "ライブラリの項目を削除します。データとセーブは保持され、同じデータを再追加すると復元されます。"))
                .setPositiveButton(tr("Remove", "Hapus", "削除"), (d,w) -> { if(!inLibrary)leaveGame(false); library.entries.remove(game); persistLibrary(); showLibrary(); })
                .setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void confirmImport(GameLibrary.Entry game) {
        settingsDialog = dialog().setTitle(tr("Import saves?", "Impor simpanan?", "セーブをインポートしますか？"))
                .setMessage(tr("Import returns to the library and replaces this language's save slots and any progress included in the archive. Export a backup first; unsaved progress is lost. Game scripts and language pack must match.",
                        "Impor kembali ke koleksi dan mengganti slot simpanan bahasa ini beserta progres dalam arsip. Ekspor cadangan dulu; progres yang belum disimpan hilang. Skrip game dan paket bahasa harus cocok.",
                        "インポートでライブラリに戻り、この言語のセーブとアーカイブ内の進行情報を置き換えます。先にバックアップしてください。未保存の進行状況は失われます。スクリプトと翻訳パックの一致が必要です。"))
                .setPositiveButton(tr("Choose archive", "Pilih arsip", "アーカイブを選択"), (d,w) -> {
                    if (!inLibrary) leaveGame(true);
                    documentGame = game; documentLanguage = game.language; pickDocument(IMPORT_SAVES, "*/*", null);
                }).setNegativeButton(tr("Cancel", "Batal", "キャンセル"), null).show();
    }
    private void pickTree(int request) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try { startActivityForResult(intent, request); } catch (RuntimeException error) { notifyUser(error.getMessage()); }
    }
    private void pickDocument(int request, String mime, String filename) {
        Intent intent = new Intent(request == EXPORT_SAVES ? Intent.ACTION_CREATE_DOCUMENT : Intent.ACTION_OPEN_DOCUMENT);
        intent.setType(mime); intent.addCategory(Intent.CATEGORY_OPENABLE);
        if (filename != null) intent.putExtra(Intent.EXTRA_TITLE, filename);
        intent.addFlags(request == EXPORT_SAVES ? Intent.FLAG_GRANT_WRITE_URI_PERMISSION : Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try { startActivityForResult(intent, request); } catch (RuntimeException error) { notifyUser(error.getMessage()); }
    }
    private interface FileTask { void run() throws Exception; }
    private void runFileTask(String label, FileTask task, Runnable success) {
        runFileTask(label, task, success, false);
    }
    private void runFileTask(String label, FileTask task, Runnable success, boolean cancellable) {
        if (ioBusy || destroyed) return;
        stopRendering(); ioBusy = true; importCancelled = new AtomicBoolean();
        LinearLayout progBox = ArisaUi.column(this);
        progBox.setPadding(dp(24), dp(12), dp(24), dp(16));
        ProgressBar bar = new ProgressBar(this);
        bar.setIndeterminate(true);
        progBox.addView(bar);
        ArisaUi.gap(progBox, 12);
        progressMessage = text(tr("Working…", "Memproses…", "処理中…"), 14, MUTED);
        progBox.addView(progressMessage);
        AlertDialog.Builder builder = dialog().setTitle(label).setView(progBox).setCancelable(false);
        if (cancellable) {
            builder.setNegativeButton(tr("Cancel", "Batal", "キャンセル"), (d, w) -> {
                importCancelled.set(true);
                if (progressMessage != null) progressMessage.setText(tr("Cancelling…", "Membatalkan…", "中止しています…"));
            });
        }
        progressDialog = builder.show();
        fileWorker.execute(() -> {
            String failure = null;
            try { task.run(); } catch (Exception error) { Log.e(TAG, "File operation failed", error); failure = error.getMessage() == null ? error.toString() : error.getMessage(); }
            final String result = failure;
            runOnUiThread(() -> {
                ioBusy = false;
                if (destroyed) return;
                if (progressDialog != null) { progressDialog.dismiss(); progressDialog = null; progressMessage = null; }
                if (result != null) dialog().setTitle(tr("Operation failed", "Operasi gagal", "操作に失敗しました"))
                        .setMessage(result).setPositiveButton("OK", null).show();
                else success.run();
                if (inLibrary) showLibrary(); else startRendering();
            });
        });
    }
    private File importBase() {
        File storage = getExternalFilesDir(null); return new File(storage == null ? getFilesDir() : storage, "imports");
    }
    private void registerPath(File path, String language, GameLibrary.Entry game, boolean owned) {
        runFileTask(tr("Checking game files", "Memeriksa berkas game", "ゲームデータを確認中"), () -> registerGame(path, language, game, owned), () -> {});
    }
    private void registerGame(File path, String overlayLanguage, GameLibrary.Entry target, boolean owned) throws IOException {
        File root = GameLibrary.dataRoot(path, overlayLanguage != null);
        if (overlayLanguage == null && probeDir(root) == null) throw new IOException("Cannot read MES/start.mes. Use the folder picker or grant storage access.");
        String fingerprint = GameLibrary.fingerprint(root);
        if (importCancelled.get()) throw new IOException("Import cancelled.");
        if (overlayLanguage != null) {
            if (overlayLanguage.equals("en")) { target.english=root.getAbsolutePath(); target.enFingerprint=fingerprint; target.enEncoding=documentLanguage; }
            else { target.indonesian=root.getAbsolutePath(); target.idFingerprint=fingerprint; target.idEncoding=documentLanguage; }
        } else {
            GameLibrary.Entry entry = library.find(fingerprint);
            if (entry == null) {
                entry = new GameLibrary.Entry(); entry.id=fingerprint; entry.title="D.C.4"; entry.path=root.getAbsolutePath(); library.entries.add(entry);
            } else if (owned && probeDir(new File(entry.path)) != null) { SaveArchive.removeTree(path); }
            else entry.path=root.getAbsolutePath();
            migrateLegacySaves(entry);
        }
        try { library.save(); }
        catch (IOException error) { library.reload(); throw error; }
    }
    private void migrateLegacySaves(GameLibrary.Entry game) throws IOException {
        File old = new File(getFilesDir(), "saves"), marker = new File(getFilesDir(), "legacy-saves-migrated");
        if (!old.isDirectory() || marker.exists()) return;
        File dest = savesDir(game, "jp"); if (!dest.isDirectory() && !dest.mkdirs()) throw new IOException("Cannot migrate old saves.");
        File[] files = old.listFiles();
        if (files != null) for (File file : files) if (file.isFile() && file.getName().matches("(?:quicksave|slot[0-9]+)\\.dc4sav(?:\\.(?:png|txt))?")) {
            File target = new File(dest, file.getName()); if (target.exists()) continue;
            try (InputStream in = new FileInputStream(file); FileOutputStream out = new FileOutputStream(target)) {
                byte[] bytes = new byte[65536]; int n; while ((n=in.read(bytes))>=0) out.write(bytes,0,n); out.getFD().sync();
            }
        }
        try (FileOutputStream out = new FileOutputStream(marker)) { out.write(game.id.getBytes(StandardCharsets.UTF_8)); out.getFD().sync(); }
    }
    @Override protected void onActivityResult(int request, int result, Intent intent) {
        super.onActivityResult(request, result, intent);
        if (result != RESULT_OK || intent == null || intent.getData() == null) return;
        final Uri uri = intent.getData(); final GameLibrary.Entry game = documentGame;
        if (request == PICK_GAME || request == PICK_OVERLAY) {
            final String overlay = request == PICK_OVERLAY ? pendingOverlayLanguage : null;
            if (overlay != null && game == null) { notifyUser("Select the game again before importing a pack."); return; }
            final File folder = new File(importBase(), UUID.randomUUID().toString());
            runFileTask(tr("Importing folder", "Mengimpor folder", "フォルダーをインポート中"), () -> {
                boolean installed = false;
                try {
                    new FolderImporter(getContentResolver(), uri, folder, importCancelled, (name, bytes) -> runOnUiThread(() -> {
                        if (progressMessage != null) progressMessage.setText(name + "\n" + (bytes / (1024 * 1024)) + " MiB");
                    })).run();
                    registerGame(folder, overlay, game, true); installed = true;
                } finally { if (!installed) SaveArchive.removeTree(folder); }
            }, () -> notifyUser(tr("Folder added. Select the content language in game settings.", "Folder ditambahkan. Pilih bahasa konten di pengaturan game.", "フォルダーを追加しました。ゲーム設定で内容の言語を選べます。")), true);
        } else if (game == null) { notifyUser("Select the game again before importing or exporting."); }
        else if (request == PICK_FONT) {
            runFileTask(tr("Importing font", "Mengimpor font", "フォントを追加中"), () -> importFont(uri, game),
                    () -> notifyUser(tr("Font imported. Reopen the game to apply it.", "Font diimpor. Buka ulang game untuk menerapkannya.", "フォントを追加しました。ゲームを再起動すると適用されます。")), true);
        } else if (request == EXPORT_SAVES || request == IMPORT_SAVES) {
            final File saves = savesDir(game, documentLanguage); final String content = game.contentId();
            runFileTask(tr("Transferring saves", "Memindahkan simpanan", "セーブを転送中"), () -> {
                if (request == EXPORT_SAVES) {
                    try (java.io.OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
                        if(out == null)throw new IOException("Cannot create archive."); SaveArchive.write(out,saves,game.id,content);
                    }
                } else {
                    try (InputStream in = getContentResolver().openInputStream(uri)) {
                        if(in == null)throw new IOException("Cannot read archive.");
                        SaveArchive.read(in, saves, game.id, content, file -> {
                            String error = dc4ValidateSaveFile(game.path, game.overlay(), game.encoding(), file.getAbsolutePath());
                            if (error != null && !error.isEmpty()) throw new IOException(error);
                        });
                    }
                }
            }, () -> notifyUser(tr("Save transfer complete.", "Pemindahan simpanan selesai.", "セーブの転送が完了しました。")));
        }
    }
    private void importFont(Uri uri, GameLibrary.Entry game) throws IOException {
        File root = new File(getFilesDir(), "fonts"); if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create font folder.");
        String name = "font.ttf";
        try (android.database.Cursor cursor = getContentResolver().query(uri, new String[]{android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) name=cursor.getString(0);
        }
        if(name==null)name="font.ttf"; name=name.replaceAll("[^\\p{L}\\p{N}._ -]", "_");
        if(name.length()>80)name=name.substring(name.length()-80); if(name.isEmpty())name="font.ttf";
        File temp = new File(root, UUID.randomUUID() + ".tmp"); boolean done=false;
        try {
            try (InputStream in=getContentResolver().openInputStream(uri); FileOutputStream out=new FileOutputStream(temp)) {
                if(in==null)throw new IOException("Cannot read font."); byte[] bytes=new byte[65536]; int n; long total=0;
                while((n=in.read(bytes))>=0){ total+=n; if(total>32*1024*1024 || importCancelled.get())throw new IOException("Font exceeds 32 MiB or import cancelled."); out.write(bytes,0,n); }
                out.getFD().sync();
            }
            byte[] signature=new byte[4]; try(InputStream in=new FileInputStream(temp)){if(in.read(signature)!=4)throw new IOException("Incomplete font.");}
            String magic=new String(signature, StandardCharsets.ISO_8859_1);
            if(!(signature[0]==0&&signature[1]==1&&signature[2]==0&&signature[3]==0)&&!magic.equals("OTTO")&&!magic.equals("ttcf")&&!magic.equals("true"))throw new IOException("Choose a TrueType or OpenType font.");
            try { Typeface.createFromFile(temp); } catch(RuntimeException error){throw new IOException("Android could not read this font.",error);}
            File target=new File(root, name); if(target.exists())target=new File(root, UUID.randomUUID().toString().substring(0,8)+"-"+name);
            if(!temp.renameTo(target))throw new IOException("Cannot install font.");
            game.font=target.getAbsolutePath(); library.save(); done=true;
        } finally {if(!done)temp.delete();}
    }

    // -------------------------------------------------------------- lifecycle

    @Override public void surfaceCreated(SurfaceHolder holder) {
        if (surface == null || holder != surface.getHolder()) return;
        surfaceReady = true;
        startRendering();
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        if (surface == null || holder != surface.getHolder()) return;
        surfaceReady = false;
        stopRendering();
    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        startRendering();
    }

    @Override public void onBackPressed() {
        if (ioBusy) return;
        if (inLibrary) super.onBackPressed();
        else menuCommands.add(() -> {if(!dc4CancelConfirmation())runOnUiThread(this::showExtras);});
    }

    @Override protected void onPause() {
        resumed = false;
        autoMode = false;
        skipMode = false;
        backlogOpen = false;
        slotMenuOpen = false;
        stopRendering();
        if (saveDialog != null) { saveDialog.dismiss(); saveDialog = null; }
        super.onPause();
    }

    // Below Android 11 the runtime permission dialog returns here, not through
    // onResume; retry resolution so a grant starts the scene immediately.
    @Override public void onRequestPermissionsResult(
            int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode == 1 && grantResults.length > 0 &&
                grantResults[0] == PackageManager.PERMISSION_GRANTED)
            showPathEntry();
    }

    @Override protected void onDestroy() {
        stopRendering();
        if (initialized) dc4Quit();
        initialized = false;
        destroyed = true;
        if (importCancelled != null) importCancelled.set(true);
        fileWorker.shutdownNow();
        if (progressDialog != null) progressDialog.dismiss();
        if (settingsDialog != null) settingsDialog.dismiss();
        if (frame != null) frame.recycle();
        super.onDestroy();
    }
}
