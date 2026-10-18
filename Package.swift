// swift-tools-version:5.9
// MetalTyrian: a native Mac port of Tyrian 2000 on OpenTyrian2000's game code.
// Build the app with scripts/build-app.sh, not swift build alone.
import PackageDescription

let package = Package(
    name: "MetalTyrian",
    platforms: [.macOS(.v13)],
    targets: [
        // MetalTyrian's own small implementation of the SDL 2 functions the game
        // uses, plus the interface the Swift app talks to (metaltyrian_host.h).
        .target(
            name: "SDLShim",
            path: "Sources/SDLShim"
        ),
        // OpenTyrian2000's game code, unchanged apart from the frame handover in
        // video.c. Its main() is renamed so the app can run it on its own thread.
        .target(
            name: "TyrianCore",
            dependencies: ["SDLShim"],
            path: "Engine",
            sources: ["src"],
            publicHeadersPath: "include",
            cSettings: [
                .define("METALTYRIAN"),
                .define("NDEBUG"),
                .define("TARGET_UNIX"),
                .define("TYRIAN_DIR", to: "\".\""),
                .define("OPENTYRIAN_VERSION", to: "\"metaltyrian\""),
                .define("main", to: "tyrian_main"),
                // The game code is 1990s-style C written for 32-bit ints; Xcode's
                // 64-to-32 warning fires on dozens of harmless file offsets and
                // counts in it, so it is off for this target only. -fno-common
                // (the default in GCC 10 and later, which builds OpenTyrian
                // upstream) puts the game's globals in ordinary data, which also
                // quiets the linker's __common alignment warning.
                .unsafeFlags(["-std=gnu99", "-Wno-shorten-64-to-32", "-fno-common"]),
            ]
        ),
        .executableTarget(
            name: "MetalTyrian",
            dependencies: ["SDLShim", "TyrianCore"],
            path: "Sources/MetalTyrian"
        ),
    ]
)
