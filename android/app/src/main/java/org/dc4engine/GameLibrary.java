package org.dc4engine;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;

/** Small, atomically persisted catalogue. All paths are real filesystem paths. */
final class GameLibrary {
    static final class Entry {
        String id, title, path, language = "jp", font = "";
        String english = "", indonesian = "", enEncoding = "windows-31j", idEncoding = "windows-31j";
        String enFingerprint = "", idFingerprint = "";
        String overlay() { return language.equals("en") ? english : language.equals("id") ? indonesian : ""; }
        String encoding() { return language.equals("en") ? enEncoding : language.equals("id") ? idEncoding : "windows-31j"; }
        String contentId() { return language.equals("en") ? enFingerprint : language.equals("id") ? idFingerprint : id; }
    }

    final ArrayList<Entry> entries = new ArrayList<>();
    private final File file;
    GameLibrary(File root) throws IOException {
        file = new File(root, "library.properties");
        File recovery = new File(file + ".backup");
        if (!file.exists() && recovery.isFile() && !recovery.renameTo(file)) throw new IOException("Cannot recover the game library.");
        Properties p = new Properties();
        if (file.isFile()) try (InputStream in = new FileInputStream(file)) { p.load(in); }
        for (String id : p.getProperty("ids", "").split(",")) {
            if (!id.matches("[a-f0-9]{64}")) continue;
            Entry e = new Entry(); e.id = id;
            e.path = p.getProperty(id + ".path", "");
            e.title = p.getProperty(id + ".title", "D.C.4");
            e.language = p.getProperty(id + ".language", "jp");
            if (!Arrays.asList("jp", "en", "id").contains(e.language)) e.language = "jp";
            e.font = p.getProperty(id + ".font", "");
            e.english = p.getProperty(id + ".en", ""); e.indonesian = p.getProperty(id + ".id", "");
            e.enEncoding = p.getProperty(id + ".enEncoding", "windows-31j");
            e.idEncoding = p.getProperty(id + ".idEncoding", "windows-31j");
            e.enFingerprint = p.getProperty(id + ".enFingerprint", "");
            e.idFingerprint = p.getProperty(id + ".idFingerprint", "");
            if (!e.language.equals("jp") && e.overlay().isEmpty()) e.language = "jp";
            entries.add(e);
        }
    }
    void save() throws IOException {
        Properties p = new Properties(); StringBuilder ids = new StringBuilder();
        for (Entry e : entries) {
            if (ids.length() != 0) ids.append(','); ids.append(e.id);
            p.setProperty(e.id + ".path", e.path); p.setProperty(e.id + ".title", e.title);
            p.setProperty(e.id + ".language", e.language); p.setProperty(e.id + ".font", e.font);
            p.setProperty(e.id + ".en", e.english); p.setProperty(e.id + ".id", e.indonesian);
            p.setProperty(e.id + ".enEncoding", e.enEncoding); p.setProperty(e.id + ".idEncoding", e.idEncoding);
            p.setProperty(e.id + ".enFingerprint", e.enFingerprint); p.setProperty(e.id + ".idFingerprint", e.idFingerprint);
        }
        p.setProperty("ids", ids.toString());
        File tmp = new File(file + ".new"), backup = new File(file + ".backup");
        try (FileOutputStream out = new FileOutputStream(tmp)) { p.store(out, "Arisa32 game library"); out.getFD().sync(); }
        if (backup.exists() && !backup.delete()) throw new IOException("Cannot update library backup");
        boolean existed = file.exists();
        if (existed && !file.renameTo(backup)) throw new IOException("Cannot back up library");
        if (!tmp.renameTo(file)) {
            if (existed && !backup.renameTo(file)) throw new IOException("Cannot restore library; backup retained");
            throw new IOException("Cannot save library");
        }
        if (backup.exists()) backup.delete();
    }
    Entry find(String id) { for (Entry e : entries) if (e.id.equals(id)) return e; return null; }
    void reload() throws IOException {
        GameLibrary restored = new GameLibrary(file.getParentFile());
        entries.clear(); entries.addAll(restored.entries);
    }
    static File dataRoot(File path, boolean overlay) throws IOException {
        if (new File(path, "MES").isDirectory() && (overlay || new File(path, "MES/start.mes").isFile())) return path;
        File[] list = path.listFiles();
        if (list != null) for (File child : list) if (child.isDirectory() && child.getName().equalsIgnoreCase("AdvData")) {
            if (new File(child, "MES").isDirectory() && (overlay || new File(child, "MES/start.mes").isFile())) return child;
        }
        throw new IOException(overlay ? "Choose a translation folder containing MES." : "Choose AdvData or its parent folder (MES/start.mes is required).");
    }
    static String fingerprint(File data) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            File root = new File(data, "MES"); ArrayList<File> scripts = new ArrayList<>();
            collect(root, scripts, 0);
            if (scripts.isEmpty()) throw new IOException("The MES folder contains no scripts.");
            Collections.sort(scripts, Comparator.comparing(f -> f.getAbsolutePath()));
            long total = 0; byte[] bytes = new byte[65536];
            for (File script : scripts) {
                String relative = root.toURI().relativize(script.toURI()).getPath();
                digest.update(relative.getBytes(StandardCharsets.UTF_8)); digest.update((byte) 0);
                try (InputStream in = new FileInputStream(script)) {
                    int n; while ((n = in.read(bytes)) >= 0) {
                        total += n; if (total > 1024L * 1024 * 1024) throw new IOException("MES scripts exceed 1 GiB.");
                        digest.update(bytes, 0, n);
                    }
                }
                digest.update((byte) 0);
            }
            StringBuilder out = new StringBuilder();
            for (byte b : digest.digest()) out.append(String.format(Locale.ROOT, "%02x", b & 255));
            return out.toString();
        } catch (java.security.NoSuchAlgorithmException error) { throw new IOException(error); }
    }
    private static void collect(File dir, ArrayList<File> out, int depth) throws IOException {
        if (depth > 12) throw new IOException("MES folders are nested too deeply.");
        File[] files = dir.listFiles(); if (files == null) throw new IOException("Cannot read " + dir);
        for (File file : files) {
            if (!file.getCanonicalPath().startsWith(dir.getCanonicalPath() + File.separator)) throw new IOException("Linked files are unsupported.");
            if (file.isDirectory()) collect(file, out, depth + 1);
            else if (file.getName().toLowerCase(Locale.ROOT).endsWith(".mes")) out.add(file);
            if (out.size() > 20000) throw new IOException("Too many script files.");
        }
    }
}
