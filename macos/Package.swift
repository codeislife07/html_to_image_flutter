// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "html_to_image_flutter",
    platforms: [
        .macOS(.v10_13)
    ],
    products: [
        .library(
            name: "html_to_image_flutter",
            targets: ["html_to_image_flutter"]
        )
    ],
    targets: [
        .target(
            name: "html_to_image_flutter",
            dependencies: [],
            path: "Classes",
            publicHeadersPath: "."
        )
    ]
)
