#/bin/sh

echo "\nCompiling launcher..."
sudo clang tools/launcher.c \
    src/logger.c \
    -o "/usr/local/bin/lockpatch_launcher" \
    -framework CoreFoundation \
    -framework Foundation || {
	echo "\nError compiling launcher!"
}

echo "\nCompiling lockpatch.dylib..."
sudo mkdir -p "/Library/Application Support/LockPatch"
sudo clang -o "/Library/Application Support/LockPatch/lockpatch.dylib" \
    src/logger.c \
    src/find_offset.c \
    src/grace_period.c \
    src/lockpatch.c \
    src/locker.c \
    src/monitor.c \
    src/main_dylib.c \
    -framework CoreFoundation \
    -framework Foundation \
    -framework IOKit \
    -dynamiclib \
    -lobjc \
    -install_name "/Library/Application Support/LockPatch/lockpatch.dylib" || {
	echo "\nError compiling lockpatch.dylib!"
}

echo "\nCompiling lockpatch CLI..."
clang \
    src/logger.c \
    src/find_offset.c \
    src/lockpatch.c \
    src/cli_main.c \
    -framework CoreFoundation \
    -o /usr/local/bin/lockpatch || {
	echo "\nError compiling lockpatch CLI!"
}

echo "\nAd-hoc signing and setting permissions..."
sudo codesign -s - --force "/Library/Application Support/LockPatch/lockpatch.dylib" 

sudo chown root:wheel "/Library/Application Support/LockPatch/lockpatch.dylib" 
sudo chmod 755 "/Library/Application Support/LockPatch/lockpatch.dylib"
sudo cp com.samrod.lockpatch.plist /Library/LaunchDaemons/

sudo touch /var/log/samrod.lockpatch.log
sudo chmod 666 /var/log/samrod.lockpatch.log

echo "\nBuild complete."

echo "Injecting dylib into loginwindow..."
sudo lldb -p $(pgrep -x loginwindow) --batch \
    -o "p (void*)dlopen((char*)\"/Library/Application Support/LockPatch/lockpatch.dylib\", 1)" \
    -o "detach" \
    -o "quit"

echo "\nInjection complete."
