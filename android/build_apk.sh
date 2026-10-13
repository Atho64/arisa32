#!/usr/bin/env bash
# Builds an installable APK without Gradle: NDK -> aapt2 -> javac -> d8 -> zipalign.
#
# Requires: ANDROID_SDK (aapt2, d8, zipalign, apksigner, android.jar), a JDK
# (javac/keytool). Defaults probe the usual Windows locations.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${ANDROID_SDK:-C:/Users/Atho/AppData/Local/Android/Sdk}"
JDK="${JAVA_HOME:-C:/Users/Atho/.jdks/jdk-17.0.20+8}"
NDK="${ANDROID_NDK_HOME:-$SDK/ndk/27.3.13750724}"
BT="$SDK/build-tools/35.0.0"
PLATFORM="$SDK/platforms/android-35/android.jar"
# aapt2 chokes on spaces in its own command line, so build in a space-free plain path.
OUT="${DC4_BUILD_DIR:-C:/dc4build/arisa32}"
MIN_SDK=24

CMAKE="$SDK/cmake/3.22.1/bin/cmake.exe"
NINJA="$SDK/cmake/3.22.1/bin/ninja.exe"

if [[ "$OUT" == "/" || "$OUT" == "" || "$OUT" == "C:/" || "$OUT" == "C:\\" ]]; then
  echo "Error: Unsafe build directory $OUT" >&2
  exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/obj" "$OUT/lib" "$OUT/classes" "$OUT/apk" "$OUT/gen/res" "$OUT/gen/r" "$OUT/staging_res"

# ---- 1. native library ------------------------------------------------------
for ABI in arm64-v8a armeabi-v7a; do
  echo "== native $ABI"
  WORKER_ABI="arm64"
  if [ "$ABI" = "armeabi-v7a" ]; then WORKER_ABI="armv7"; fi
  PREBUILT="$ROOT/android/app/src/main/cpp/build-worker-$WORKER_ABI/libdc4.so"
  mkdir -p "$OUT/lib/$ABI"
  if [ -f "$PREBUILT" ]; then
    echo "Using worker build $PREBUILT"
    cp "$PREBUILT" "$OUT/lib/$ABI/libdc4.so"
  else
    "$CMAKE" -S "$ROOT/android/app/src/main/cpp" -B "$OUT/obj/$ABI" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-$MIN_SDK \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="$NINJA" >/dev/null
    "$CMAKE" --build "$OUT/obj/$ABI" >/dev/null
    cp "$OUT/obj/$ABI/libdc4.so" "$OUT/lib/$ABI/"
  fi
done

# ---- 2. resources + manifest -> R.java + APK base ----------------------------
echo "== aapt2 compile"
cp -r "$ROOT/android/app/src/main/res/." "$OUT/staging_res/"
"$BT/aapt2.exe" compile --dir "$OUT/staging_res" -o "$OUT/gen/res.zip"

echo "== aapt2 link"
"$BT/aapt2.exe" link -o "$OUT/apk/base.apk" -I "$PLATFORM" \
  --manifest "$ROOT/android/app/src/main/AndroidManifest.xml" \
  --min-sdk-version $MIN_SDK --target-sdk-version 35 \
  --version-code 2 --version-name "0.2.0" \
  --java "$OUT/gen/r" \
  "$OUT/gen/res.zip"

# ---- 3. java bytecode -> dex ------------------------------------------------
echo "== javac"
mkdir -p "$OUT/java/org/dc4engine"
cp "$ROOT"/android/app/src/main/java/org/dc4engine/*.java "$OUT/java/org/dc4engine/"
cp "$OUT"/gen/r/org/dc4engine/*.java "$OUT/java/org/dc4engine/"
"$JDK/bin/javac" -encoding UTF-8 -source 11 -target 11 -classpath "$PLATFORM" \
  -d "$OUT/classes" "$OUT/java/org/dc4engine/"*.java
CLASS_ARGS=()
while IFS= read -r f; do CLASS_ARGS+=("$f"); done < <(find "$OUT/classes" -name '*.class')
"$BT/d8.bat" --release --min-api $MIN_SDK --lib "$PLATFORM" \
  --output "$OUT/apk" "${CLASS_ARGS[@]}"

# ---- 4. pack ----------------------------------------------------------------
echo "== pack"
PY_EXE="${DC4_PYTHON:-python}"
"$PY_EXE" - "$OUT" <<'PYEOF2'
import os, shutil, sys, zipfile
out = sys.argv[1]
apk = os.path.join(out, "apk")
base = os.path.join(apk, "base.apk")
dst = os.path.join(apk, "unpacked")
if os.path.isdir(dst):
    shutil.rmtree(dst)
os.makedirs(dst)
with zipfile.ZipFile(base) as z:
    z.extractall(dst)
shutil.copytree(os.path.join(out, "lib"), os.path.join(dst, "lib"),
                dirs_exist_ok=True)
shutil.copy(os.path.join(apk, "classes.dex"), dst)
# Android wants the native libs and dex stored uncompressed and aligned; the
# final alignment is zipalign's job, so just avoid recompressing them.
src = zipfile.ZipFile(os.path.join(out, "unsigned.apk"), "w",
                      zipfile.ZIP_DEFLATED)
for root, _dirs, files in os.walk(dst):
    for f in files:
        full = os.path.join(root, f)
        rel = os.path.relpath(full, dst).replace(os.sep, "/")
        # Targeting R+ (API 30+): resources.arsc must be stored uncompressed;
        # zipalign below restores its 4-byte alignment.
        method = zipfile.ZIP_STORED if rel == "resources.arsc" else zipfile.ZIP_DEFLATED
        src.write(full, rel, compress_type=method)
src.close()
print("packed", os.path.join(out, "unsigned.apk"))
PYEOF2

# ---- 5. sign ----------------------------------------------------------------
echo "== sign"
KS="$ROOT/android/debug.keystore"
if [ ! -f "$KS" ]; then
  "$JDK/bin/keytool" -genkeypair -keystore "$KS" -storepass dc4dc4 \
    -keypass dc4dc4 -alias dc4 -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Arisa32" >/dev/null 2>&1
fi
"$BT/zipalign.exe" -f 4 "$OUT/unsigned.apk" "$OUT/apk/aligned.apk"
"$BT/apksigner.bat" sign --ks "$KS" --ks-pass pass:dc4dc4 \
  --key-pass pass:dc4dc4 --out "$OUT/Arisa32-debug.apk" "$OUT/apk/aligned.apk"

echo
cp "$OUT/Arisa32-debug.apk" "$ROOT/android/Arisa32-debug.apk" 2>/dev/null || true
cp "$OUT/Arisa32-debug.apk" "$ROOT/android/dc4-debug.apk" 2>/dev/null || true
echo "APK: $ROOT/android/Arisa32-debug.apk"
ls -la "$ROOT/android/Arisa32-debug.apk"
