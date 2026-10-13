package org.dc4engine;

import java.io.*;
import java.util.*;
import java.util.zip.*;

/** Portable emulator saves only. Imports replace a complete save set atomically. */
final class SaveArchive {
    interface Validator { void check(File save) throws IOException; }
    private static final long MAX_FILE = 32L * 1024 * 1024, MAX_TOTAL = 256L * 1024 * 1024;
    private static final int MAX_ENTRIES = 310;
    private static boolean allowed(String name) {
        return name.matches("(?:quicksave|autosave|slot(?:[1-9]|[1-9][0-9]|100))\\.dc4sav(?:\\.(?:png|txt))?") ||
                name.equals("system-profile.dat") || name.equals("read-lines.txt");
    }
    static void write(OutputStream output, File saves, String game, String content) throws IOException {
        recover(saves);
        File[] available = saves.listFiles(); boolean any = false;
        if (available != null) for (File file : available) if (file.isFile() && allowed(file.getName()) &&
                (file.getName().endsWith(".dc4sav") || file.getName().equals("system-profile.dat") || file.getName().equals("read-lines.txt"))) any = true;
        if (!any) throw new IOException("There are no saves or progress to export.");
        try (ZipOutputStream zip = new ZipOutputStream(output)) {
            Properties manifest = new Properties();
            manifest.setProperty("format", "dc4engine-saves-1"); manifest.setProperty("game", game);
            manifest.setProperty("content", content);
            zip.putNextEntry(new ZipEntry("manifest.properties")); manifest.store(zip, "DC4 emulator save archive"); zip.closeEntry();
            File[] files = saves.listFiles(); long total = 0; int entries = 1;
            if (files != null) for (File file : files) {
                if (!file.isFile() || !allowed(file.getName())) continue;
                if (++entries > MAX_ENTRIES || file.length() > MAX_FILE || (total += file.length()) > MAX_TOTAL)
                    throw new IOException("Save set exceeds archive limits.");
                if (!file.getCanonicalFile().getParentFile().equals(saves.getCanonicalFile())) throw new IOException("Linked saves are unsupported.");
                if (file.getName().equals("system-profile.dat") || file.getName().equals("read-lines.txt")) validateMetadata(file);
                zip.putNextEntry(new ZipEntry(file.getName()));
                try (InputStream in = new FileInputStream(file)) { copy(in, zip, MAX_FILE); }
                zip.closeEntry();
            }
        }
    }
    static void read(InputStream input, File saves, String game, String content) throws IOException {
        read(input, saves, game, content, file -> {});
    }
    static void read(InputStream input, File saves, String game, String content, Validator validator) throws IOException {
        recover(saves);
        File parent = saves.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create save folder.");
        File staging = new File(parent, "import-" + UUID.randomUUID()), backup = new File(parent, saves.getName() + ".import-backup");
        if (!staging.mkdir()) throw new IOException("Cannot stage save import.");
        boolean committed = false;
        try {
            Properties manifest = null; Set<String> seen = new HashSet<>(); long total = 0; int saveCount = 0;
            try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(input))) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    String name = entry.getName();
                    if (entry.isDirectory() || !seen.add(name) || seen.size() > MAX_ENTRIES)
                        throw new IOException("Invalid or duplicate archive entry.");
                    if (name.equals("manifest.properties")) {
                        ByteArrayOutputStream bytes = new ByteArrayOutputStream(); copy(zip, bytes, 16384);
                        manifest = new Properties(); manifest.load(new ByteArrayInputStream(bytes.toByteArray()));
                    } else {
                        if (!allowed(name)) throw new IOException("Unexpected save entry: " + name);
                        File dest = new File(staging, name);
                        if (!dest.getCanonicalFile().getParentFile().equals(staging.getCanonicalFile())) throw new IOException("Unsafe archive path.");
                        long limit = name.equals("read-lines.txt") ? MAX_FILE : name.equals("system-profile.dat") ? 4L * 1024 * 1024 :
                                name.endsWith(".txt") ? 4096 : name.endsWith(".png") ? 100000 : MAX_FILE;
                        try (FileOutputStream out = new FileOutputStream(dest)) { total += copy(zip, out, limit); out.getFD().sync(); }
                        if (total > MAX_TOTAL) throw new IOException("Archive exceeds 256 MiB.");
                        if (name.endsWith(".dc4sav")) {
                            validateSave(dest);
                            ++saveCount;
                        } else if (name.equals("system-profile.dat") || name.equals("read-lines.txt")) validateMetadata(dest);
                    }
                    zip.closeEntry();
                }
            }
            if (manifest == null || !"dc4engine-saves-1".equals(manifest.getProperty("format"))) throw new IOException("Not a DC4 emulator save archive.");
            if (!game.equals(manifest.getProperty("game"))) throw new IOException("This archive belongs to different game data.");
            if (!content.equals(manifest.getProperty("content"))) throw new IOException("This archive uses different language scripts. Select the matching game language first.");
            if (saveCount == 0 && !seen.contains("system-profile.dat") && !seen.contains("read-lines.txt")) throw new IOException("The archive contains no saves or progress.");
            for (String name : seen) if (!name.equals("read-lines.txt") && (name.endsWith(".png") || name.endsWith(".txt")) &&
                    !seen.contains(name.substring(0, name.length() - 4))) throw new IOException("Save preview has no matching save.");
            for (String name : seen) if (name.endsWith(".dc4sav")) validator.check(new File(staging, name));
            // Older archives contain only manual slots. Preserve unrelated
            // progress and automatic saves unless the archive supplies them.
            for (String name : new String[]{"system-profile.dat", "read-lines.txt", "autosave.dc4sav", "autosave.dc4sav.png", "autosave.dc4sav.txt"}) {
                File existing = new File(saves, name);
                if (seen.contains(name) || !existing.isFile()) continue;
                if (name.startsWith("autosave.") && seen.contains("autosave.dc4sav")) continue;
                if (!existing.getCanonicalFile().getParentFile().equals(saves.getCanonicalFile())) throw new IOException("Linked save metadata is unsupported.");
                try (InputStream in = new FileInputStream(existing); FileOutputStream out = new FileOutputStream(new File(staging, name))) {
                    copy(in, out, MAX_FILE); out.getFD().sync();
                }
            }
            boolean hadSaves = saves.exists();
            if (hadSaves && !saves.renameTo(backup)) throw new IOException("Cannot preserve existing saves.");
            if (!staging.renameTo(saves)) {
                if (hadSaves && !backup.renameTo(saves)) throw new IOException("Import failed; existing saves retained at " + backup);
                throw new IOException("Cannot install imported saves; existing saves restored.");
            }
            committed = true;
        } finally { removeTree(staging); if (committed) removeTree(backup); }
    }
    /** Finish or roll back an interrupted directory swap after process death. */
    static void recover(File saves) throws IOException {
        File backup = new File(saves.getParentFile(), saves.getName() + ".import-backup");
        if (!backup.exists()) return;
        if (!saves.exists()) {
            if (!backup.renameTo(saves)) throw new IOException("Cannot restore interrupted save import; backup retained at " + backup);
        } else removeTree(backup);
    }
    private static long copy(InputStream in, OutputStream out, long limit) throws IOException {
        long total = 0; byte[] bytes = new byte[65536]; int n;
        while ((n = in.read(bytes)) >= 0) { total += n; if (total > limit) throw new IOException("Archive file exceeds its size limit."); out.write(bytes, 0, n); }
        return total;
    }
    private static void validateSave(File file) throws IOException {
        if (file.length() < 24 || file.length() > 2 * 1024 * 1024 + 24) throw new IOException("Invalid save size.");
        try (DataInputStream in = new DataInputStream(new FileInputStream(file))) {
            byte[] magic = new byte[8]; in.readFully(magic);
            if (!Arrays.equals(magic, new byte[]{'D','C','4','S','A','V','E',0})) throw new IOException("Not a DC4 save.");
            int version = Integer.reverseBytes(in.readInt()), length = Integer.reverseBytes(in.readInt());
            long expected = Long.reverseBytes(in.readLong()), hash = 0xcbf29ce484222325L;
            if (version < 1 || version > 4 || length != file.length() - 24) throw new IOException("Unsupported or truncated save.");
            byte[] bytes = new byte[65536]; int n;
            while ((n = in.read(bytes)) >= 0) for (int i = 0; i < n; ++i) { hash ^= bytes[i] & 255; hash *= 1099511628211L; }
            if (hash != expected) throw new IOException("Save checksum mismatch.");
        }
    }
    private static void validateMetadata(File file) throws IOException {
        boolean profile = file.getName().equals("system-profile.dat");
        try (BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(file), java.nio.charset.StandardCharsets.ISO_8859_1))) {
            if (profile && !"DC4ENGINE_PROFILE 1".equals(in.readLine())) throw new IOException("Unsupported progress profile.");
            int count = 0; String line;
            while ((line = in.readLine()) != null) {
                if (++count > (profile ? 10000 : 100000) || line.length() > (profile ? 4096 : 300) || line.indexOf('\0') >= 0)
                    throw new IOException("Progress metadata exceeds limits.");
                if (!profile || line.trim().isEmpty()) continue;
                String[] words = line.trim().split("\\s+");
                try {
                    if (words[0].equals("v")) {
                        if (words.length != 4) throw new IOException("Invalid profile variable.");
                        int bank = Integer.parseInt(words[1]), index = Integer.parseInt(words[2]), value = Integer.parseInt(words[3]);
                        if (bank < 93 || bank > 100 || index < 0 || index > 4095 || Math.abs((long)value) > 1000000) throw new IOException("Invalid profile variable.");
                    } else if (words[0].equals("p")) {
                        if (words.length != 2) throw new IOException("Invalid playlist entry.");
                        int id = Integer.parseInt(words[1]); if (id < 0 || id > 1000) throw new IOException("Invalid playlist entry.");
                    } else if (words[0].equals("c") || words[0].equals("m") || words[0].equals("s")) {
                        String quoted = line.trim().substring(1).trim();
                        if (quoted.length() < 2 || quoted.charAt(0) != '"' || quoted.charAt(quoted.length()-1) != '"') throw new IOException("Invalid profile asset.");
                        StringBuilder name = new StringBuilder();
                        for (int i=1;i<quoted.length()-1;i++) {
                            char ch = quoted.charAt(i);
                            if (ch == '\\') { if (++i >= quoted.length()-1) throw new IOException("Invalid profile escape."); ch = quoted.charAt(i); }
                            else if (ch == '"') throw new IOException("Invalid profile asset.");
                            name.append(ch);
                        }
                        String asset=name.toString();
                        if(asset.isEmpty() || asset.length()>=512 || asset.startsWith("/") || asset.startsWith("\\") || asset.contains(":") || asset.contains("..")) throw new IOException("Invalid profile asset.");
                    } else throw new IOException("Unknown progress profile record.");
                } catch (NumberFormatException error) { throw new IOException("Invalid progress profile number.", error); }
            }
        }
    }
    static void removeTree(File root) {
        if (!root.exists()) return;
        try { removeOwned(root, root.getCanonicalPath()); } catch (IOException ignored) { }
    }
    private static void removeOwned(File file, String root) throws IOException {
        String path = file.getCanonicalPath();
        if (!path.equals(root) && !path.startsWith(root + File.separator)) return;
        File[] children = file.listFiles(); if (children != null) for (File child : children) removeOwned(child, root);
        file.delete();
    }
}
