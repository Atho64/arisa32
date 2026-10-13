package org.dc4engine;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.os.StatFs;
import java.io.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;

/** SAF documents are copied to an app-owned folder; content URIs are never native paths. */
final class FolderImporter {
    interface Progress { void update(String file, long bytes); }
    private final ContentResolver resolver;
    private final Uri tree;
    private final File destination;
    private final AtomicBoolean cancelled;
    private final Progress progress;
    private final Set<String> ancestors = new HashSet<>();
    private long bytes, lastUpdate, available;
    private int files;
    FolderImporter(ContentResolver resolver, Uri tree, File destination, AtomicBoolean cancelled, Progress progress) {
        this.resolver = resolver; this.tree = tree; this.destination = destination; this.cancelled = cancelled; this.progress = progress;
    }
    void run() throws IOException {
        if (!destination.mkdirs()) throw new IOException("Cannot create import folder.");
        available = new StatFs(destination.getAbsolutePath()).getAvailableBytes() - 32L * 1024 * 1024;
        boolean done = false;
        try { walk(findGameRoot(DocumentsContract.getTreeDocumentId(tree)), destination, 0); done = true; }
        finally { if (!done) SaveArchive.removeTree(destination); }
    }
    private String findGameRoot(String id) throws IOException {
        String parentData = null; boolean mes = false;
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        String[] fields = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor cursor = resolver.query(children, fields, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot inspect the selected folder.");
            while (cursor.moveToNext()) if (DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2))) {
                String name = cursor.getString(1);
                if ("MES".equals(name)) mes = true;
                if ("AdvData".equalsIgnoreCase(name)) parentData = cursor.getString(0);
            }
        }
        if (mes) return id;
        if (parentData != null) return parentData;
        throw new IOException("Choose the game or translation folder containing MES, or the parent of AdvData.");
    }
    private void walk(String id, File dir, int depth) throws IOException {
        if (depth > 20 || !ancestors.add(id)) throw new IOException("Folder is recursive or too deeply nested.");
        checkCancelled(); Set<String> names = new HashSet<>();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
        String[] projection = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE};
        try (Cursor cursor = resolver.query(children, projection, null, null, null)) {
            if (cursor == null) throw new IOException("Cannot read selected folder.");
            while (cursor.moveToNext()) {
                checkCancelled();
                if (++files > 100000) throw new IOException("Folder has too many entries.");
                String childId = cursor.getString(0), name = cursor.getString(1), mime = cursor.getString(2);
                if (name == null || name.isEmpty() || name.equals(".") || name.equals("..") || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 ||
                        name.indexOf('\0') >= 0 || !names.add(name)) throw new IOException("Folder contains an unsafe or duplicate filename.");
                File target = new File(dir, name);
                if (!target.getCanonicalPath().startsWith(destination.getCanonicalPath() + File.separator)) throw new IOException("Unsafe folder path.");
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (!target.mkdir()) throw new IOException("Cannot create " + name);
                    walk(childId, target, depth + 1);
                } else {
                    long size = cursor.isNull(3) ? 0 : cursor.getLong(3);
                    if (size > 8L * 1024 * 1024 * 1024 || size > available - bytes) throw new IOException("Insufficient space for " + name);
                    Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, childId);
                    try (InputStream in = resolver.openInputStream(document); FileOutputStream out = new FileOutputStream(target)) {
                        if (in == null) throw new IOException("Cannot read " + name);
                        byte[] buffer = new byte[131072]; int n; long fileBytes = 0;
                        while ((n = in.read(buffer)) >= 0) {
                            checkCancelled(); bytes += n; fileBytes += n;
                            if (bytes > available || bytes > 64L * 1024 * 1024 * 1024 || fileBytes > 8L * 1024 * 1024 * 1024)
                                throw new IOException("Import exceeds available space or size limit.");
                            out.write(buffer, 0, n);
                            long now = System.currentTimeMillis();
                            if (now - lastUpdate > 250) { lastUpdate = now; progress.update(name, bytes); }
                        }
                    }
                }
            }
        } catch (SecurityException | IllegalArgumentException error) { throw new IOException("The document provider could not read this folder.", error); }
        finally { ancestors.remove(id); }
    }
    private void checkCancelled() throws IOException { if (cancelled.get() || Thread.currentThread().isInterrupted()) throw new IOException("Import cancelled."); }
}
