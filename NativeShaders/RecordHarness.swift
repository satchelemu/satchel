import Foundation

@main struct RecordHarness {
    static func main() {
        while let line=readLine() {
            do {
                guard let r=try JSONSerialization.jsonObject(with:Data(line.utf8)) as? [String:Any],
                      let path=r["input"] as? String,let output=r["output"] as? String,let stage=r["stage"] as? String else{
                    throw ShaderTransformError.unsupported("invalid record fixture")
                }
                let record=try Data(contentsOf:URL(fileURLWithPath:path))
                let normalized=try ShaderRecord.normalize(record,stage:stage,facts:r["facts"] as? [String:Any],expectedKey:r["key"] as? String)
                try normalized.code.write(to:URL(fileURLWithPath:output),options:.atomic)
                let result=try JSONSerialization.data(withJSONObject:["status":"ok","job":normalized.job],options:[.sortedKeys])
                print(String(decoding:result,as:UTF8.self))
            }catch{print("{\"status\":\"error\"}")}
            fflush(stdout)
        }
    }
}
