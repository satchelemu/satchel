import Foundation
import CryptoKit

struct NormalizedShader {
    let key: String
    let code: Data
    let job: [String: Any]
}
enum ShaderRecord {
    static func word(_ data: Data, _ offset: Int) throws -> UInt32 {
        guard offset >= 0, offset <= data.count - 4 else { throw ShaderTransformError.unsupported("shader record bounds") }
        return data.withUnsafeBytes { UInt32(littleEndian: $0.loadUnaligned(fromByteOffset: offset, as: UInt32.self)) }
    }
    static func short(_ data: Data, _ offset: Int) throws -> Int {
        guard offset >= 0, offset <= data.count - 2 else { throw ShaderTransformError.unsupported("shader record bounds") }
        return data.withUnsafeBytes { Int(UInt16(littleEndian: $0.loadUnaligned(fromByteOffset: offset, as: UInt16.self))) }
    }
    static func put(_ value: UInt32, _ data: inout Data, _ offset: Int) {
        var word = value.littleEndian
        withUnsafeBytes(of: &word) { data.replaceSubrange(offset..<offset+4, with: $0) }
    }
    static func normalize(_ record: Data, stage: String, facts: [String: Any]?, expectedKey: String? = nil,
                          skippedControlRanges: [Range<Int>] = []) throws -> NormalizedShader {
        guard record.count >= 0x38, record.prefix(8) == Data("BTCDBTCD".utf8), ["vs","ps","cs","gs","hs","ds"].contains(stage) else {
            throw ShaderTransformError.unsupported("invalid shader record header")
        }
        let controlSize = Int(try word(record, 0x28)), codeSize = Int(try word(record, 0x2c))
        guard controlSize >= 0x760, codeSize >= 32, codeSize <= 16 * 1024 * 1024,
              0x38 + codeSize + controlSize == record.count else { throw ShaderTransformError.unsupported("invalid shader record lengths") }
        let ci = 0x38 + codeSize
        let control = record.subdata(in: ci..<record.count)
        var code = record.subdata(in: 0x38..<ci)
        // Switch 2 (NVN2, control version 0xc9) records take the pinned engine's Ampere route, which this Switch app does not offer.
        if try word(control,0) == 0x98761234, try word(control,4) == 0xc9 { throw ShaderTransformError.unsupported("NVN2 shader record") }
        guard try word(control,0) == 0x98761234, Int(try word(control,0x704)) == code.count else { throw ShaderTransformError.unsupported("invalid shader control") }
        let declared = Int(try word(control,0x20)), keyLength = (0x760...0x880).contains(declared) ? declared : 0x880
        guard keyLength <= control.count else { throw ShaderTransformError.unsupported("short shader control") }
        var keyControl = control.prefix(keyLength)
        for range in skippedControlRanges {
            guard range.lowerBound >= 0, range.upperBound <= keyControl.count else { throw ShaderTransformError.unsupported("invalid shader key mask") }
            keyControl.replaceSubrange(range,with:repeatElement(UInt8(0),count:range.count))
        }
        var hash=SHA256();hash.update(data:keyControl);hash.update(data:code)
        let key=hash.finalize().prefix(8).map{String(format:"%02x",$0)}.joined()
        if let expectedKey, expectedKey != key { throw ShaderTransformError.unsupported("shader identity mismatch") }
        let c1offset = Int(try word(control,0x700)), c1end = Int(try word(control,0x704))
        guard c1offset <= c1end, c1end <= code.count else { throw ShaderTransformError.unsupported("invalid c1 range") }
        var c1 = code.subdata(in:c1offset..<c1end)
        if stage == "vs", let merged=try vertexAB(code,control:control) { code=merged.0;c1=merged.1 }
        if stage != "cs" {
            guard code.count >= 0x80 else { throw ShaderTransformError.unsupported("short graphics shader") }
            code=code.subdata(in:0x30..<code.count)
        } else if try word(code,0) == 0x12345679 {
            guard code.count >= 0x100, code[0x30..<0x100].allSatisfy({$0 == 0}) else { throw ShaderTransformError.unsupported("unexpected compute program start") }
            code=code.subdata(in:0x100..<code.count)
        }
        var job:[String:Any]=["key":key,"stage":stage,"tex":[String:String](),"img":[String:Any](),"attr":[String:String](),"fout":[String:String](),
            "c1":c1.map{String(format:"%02x",$0)}.joined(),"debug":true,"reduced":true]
        if ["vs","gs"].contains(stage),let tf=try feedbackLayout(control) { job["tf"]=tf }
        if stage == "ps",control[0x718] != 0,try word(code,19*4)&2 == 0 {job["early_fragment_tests"]=true}
        if let facts {job.merge(facts){_,new in new}}else{job["no_facts"]=true}
        return NormalizedShader(key:key,code:code,job:job)
    }
    static func vertexAB(_ code:Data,control:Data)throws->(Data,Data)? {
        guard code.count>=0x80,try word(code,0)==0x12345678 else{return nil}
        let w0=try word(code,0x30)
        guard w0&0x1f==1,(w0>>10)&0xf==0 else{return nil}
        let values=try (0..<6).map{Int(try word(control,0x748+$0*4))}
        let a=values[0],alen=values[1],b=values[3],blen=values[4]
        if a==0 && alen==0 && b==0 && blen==0{return nil}
        guard a==0x30,b==Int(try word(control,0x700))+0x30,b>=0x80,b<=code.count-0x50,
              alen>=0x50,blen>=0x50,(alen-0x50)%32==0,(blen-0x50)%32==0,b+blen<=code.count,a+alen<=code.count else{
            throw ShaderTransformError.unsupported("invalid VertexA and VertexB layout")
        }
        let bw0=try word(code,b)
        guard bw0&0x1f==1,(bw0>>10)&0xf==1 else{throw ShaderTransformError.unsupported("invalid VertexB header")}
        let sa=try (0..<20).map{try word(code,a+$0*4)},sb=try (0..<20).map{try word(code,b+$0*4)}
        var sph=Data(count:0x50)
        put(sb[0] | sa[0] & ~(0xf<<10),&sph,0)
        for i in 1...3{put(max(sa[i]&0xffffff,sb[i]&0xffffff)|sb[i] & ~0xffffff,&sph,i*4)}
        put(sb[4],&sph,16)
        for i in 5..<20{put(sa[i]|sb[i],&sph,i*4)}
        var ai=code.subdata(in:a+0x50..<a+alen)
        for o in stride(from:0,to:ai.count,by:8) where o%32 != 0 {
            let instruction=ai.withUnsafeBytes{UInt64(littleEndian:$0.loadUnaligned(fromByteOffset:o,as:UInt64.self))}
            if instruction>>52==0xe30 && instruction&0x1f==0xf {
                var branch=(UInt64(0xe24)<<52 | UInt64((ai.count-(o+8))&0xffffff)<<20 | instruction&0xfffff).littleEndian
                withUnsafeBytes(of:&branch){ai.replaceSubrange(o..<o+8,with:$0)}
            }
        }
        return (code.prefix(0x30)+sph+ai+code.subdata(in:b+0x50..<b+blen),code.subdata(in:b+blen..<code.count))
    }
    static func feedbackLayout(_ control:Data)throws->[Any]? {
        let strides=try (0..<4).map{try short(control,0x6dc+$0*2)}
        if strides.allSatisfy({$0==0}){return nil}
        var streams:[Any]=[]
        for b in 0..<4 {
            if strides[b]==0{streams.append([Int]());continue}
            let count=Int(control[0x6e8+b])
            guard (1...128).contains(count) else{return nil}
            let locations=Array(control[0xdc+0x80*b..<0xdc+0x80*b+count]).map(Int.init)
            let sizes=Array(control[0x4dc+0x80*b..<0x4dc+0x80*b+count]).map(Int.init)
            guard locations.allSatisfy({(0x20..<0x40).contains($0)}),sizes.allSatisfy({(1...4).contains($0)}),strides[b]==4*sizes.reduce(0,+) else{return nil}
            streams.append(["stride":strides[b],"outputs":zip(locations,sizes).map{[$0-0x20,$1]}])
        }
        return streams
    }
}
