import Foundation
@main struct AnalysisHarness {
    static func main() throws {
        while let line=readLine() {
            guard let request=try JSONSerialization.jsonObject(with:Data(line.utf8)) as? [String:Any],
                  let source=request["source"] as? String,let expressions=request["expressions"] as? [String] else{throw ShaderTransformError.unsupported("invalid analysis fixture")}
            let definitions=ShaderBufferAnalysis.definitions(source)
            let bounds=expressions.map{expression->[Any] in
                let value=ShaderInterval(definitions).expression(expression)
                return [value.low.map{$0 as Any} ?? NSNull(),value.high.map{$0 as Any} ?? NSNull()]
            }
            let data=try JSONSerialization.data(withJSONObject:bounds)
            print(String(decoding:data,as:UTF8.self));fflush(stdout)
        }
    }
}
