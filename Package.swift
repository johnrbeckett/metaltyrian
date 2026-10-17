// swift-tools-version:5.9
// MetalTyrian: a native Mac port of Tyrian 2000 on OpenTyrian2000's game code.
// Build the app with scripts/build-app.sh, not swift build alone.
import PackageDescription

let package = Package(
    name: "MetalTyrian",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "MetalTyrian",
            path: "Sources/MetalTyrian"
        ),
    ]
)
