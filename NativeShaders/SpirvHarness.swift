import Foundation
@main struct SpirvHarness {
    static func main() throws {
        let args=CommandLine.arguments
        guard args.count==3 else{throw ShaderTransformError.unsupported("invalid SPIR-V fixture arguments")}
        let input=try Data(contentsOf:URL(fileURLWithPath:args[1]))
        try ShaderSpirv.singleton(input).write(to:URL(fileURLWithPath:args[2]),options:.atomic)
    }
}
