import Foundation

struct ShaderBounds {
    var low: Int64?
    var high: Int64?
    static let unknown = ShaderBounds(low: nil, high: nil)
    static let boolean = ShaderBounds(low: 0, high: 1)
}
final class ShaderInterval {
    let definitions: [String: (String, String)]
    var memo: [String: ShaderBounds] = [:]
    var depth = 0
    var tokens: [String] = []
    var position = 0
    enum Invalid: Error { case expression }
    static let priority = ["||":1,"&&":2,"|":3,"^":4,"&":5,"==":6,"!=":6,"<":7,">":7,"<=":7,">=":7,"<<":8,">>":8,"+":9,"-":9,"*":10,"/":10,"%":10]
    init(_ definitions: [String: (String, String)]) { self.definitions = definitions }
    func expression(_ original: String) -> ShaderBounds {
        let saved = (tokens, position)
        defer { (tokens, position) = saved }
        var text = sub(#"\bas_type<u(?:int|short|long)>\s*\("#, original) { _ in "as_type_u(" }
        text = sub(#"\bas_type<\w+>\s*\("#, text) { _ in "as_type(" }
        let pattern = #"\s*(0[xX][0-9a-fA-F]+[uU]?|\d+\.\d*(?:[eE][-+]?\d+)?f?|\d+[uU]?|[A-Za-z_]\w*(?:\.\w+)*|\.\w+|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^~!?:(),<>\[\]])"#
        tokens = []; position = 0
        var end = 0, nesting = 0
        for m in matches(pattern, text) {
            if m.start != end { return .unknown }
            if ["(","["].contains(m[1]) { nesting += 1 }
            if [")","]"].contains(m[1]) { nesting -= 1 }
            if nesting > 128 { return .unknown }
            tokens.append(m[1]); end = m.end
        }
        guard trim(slice(text, end)).isEmpty, tokens.count <= 4096 else { return .unknown }
        do { let bounds = try ternary(); return position == tokens.count ? bounds : .unknown }
        catch { return .unknown }
    }
    func variable(_ name: String) -> ShaderBounds {
        if let bounds = memo[name] { return bounds }
        guard let (kind, text) = definitions[name], depth <= 32 else { return .unknown }
        memo[name] = .unknown; depth += 1
        var bounds = expression(text); depth -= 1
        if kind == "bool" { bounds = .boolean }
        else if kind.hasPrefix("u") && (bounds.low == nil || bounds.low! < 0) { bounds = ShaderBounds(low: 0, high: nil) }
        memo[name] = bounds; return bounds
    }
    func peek() -> String? { position < tokens.count ? tokens[position] : nil }
    @discardableResult func take(_ expected: String? = nil) throws -> String {
        guard let t = peek(), expected == nil || expected == t else { throw Invalid.expression }
        position += 1; return t
    }
    func ternary() throws -> ShaderBounds {
        let condition = try binary(1)
        guard peek() == "?" else { return condition }
        try take("?"); let a = try ternary(); try take(":"); let b = try ternary()
        return ShaderBounds(low: both(a.low,b.low,min), high: both(a.high,b.high,max))
    }
    func binary(_ level: Int) throws -> ShaderBounds {
        var left = try unary()
        while let op = peek(), let priority = Self.priority[op], priority >= level {
            try take(); let right = try binary(priority + 1); left = combine(op,left,right)
        }
        return left
    }
    func unary() throws -> ShaderBounds {
        if peek() == "-" { try take(); let v = try unary(); return ShaderBounds(low: both(0,v.high,minus),high:both(0,v.low,minus)) }
        if peek() == "+" { try take(); return try unary() }
        if peek() == "!" { try take(); _ = try unary(); return .boolean }
        if peek() == "~" || peek() == "*" { try take(); _ = try unary(); return .unknown }
        var result = try primary()
        while let t = peek(), t == "[" || t.hasPrefix(".") {
            if try take() == "[" { _ = try ternary(); try take("]") }
            result = .unknown
        }
        return result
    }
    func primary() throws -> ShaderBounds {
        let t = try take()
        if t == "(" { let v = try ternary(); try take(")"); return v }
        if match(#"^0[xX][0-9a-fA-F]+[uU]?$|^\d+[uU]?$"#, t) != nil {
            let raw = t.trimmingCharacters(in: CharacterSet(charactersIn: "uU"))
            let v = raw.lowercased().hasPrefix("0x") ? Int64(raw.dropFirst(2),radix:16) : Int64(raw)
            return ShaderBounds(low:v,high:v)
        }
        if match(#"^\d+\.\d*(?:[eE][-+]?\d+)?f?$"#,t) != nil { return .unknown }
        guard match(#"^[A-Za-z_]\w*(?:\.\w+)*$"#,t) != nil else { throw Invalid.expression }
        if peek() == "(" {
            try take("("); var args: [ShaderBounds] = []
            if peek() != ")" { args.append(try ternary()); while peek() == "," { try take(","); args.append(try ternary()) } }
            try take(")"); return call(t,args)
        }
        if match(#"^_\d+$"#,t) != nil { return variable(t) }
        return t == "true" || t == "false" ? .boolean : .unknown
    }
    func both(_ a: Int64?, _ b: Int64?, _ f: (Int64,Int64)->Int64?) -> Int64? { guard let a, let b else { return nil }; return f(a,b) }
    func plus(_ a: Int64,_ b: Int64)->Int64? { let r=a.addingReportingOverflow(b);return r.overflow ? nil:r.partialValue }
    func minus(_ a: Int64,_ b: Int64)->Int64? { let r=a.subtractingReportingOverflow(b);return r.overflow ? nil:r.partialValue }
    func multiply(_ a: Int64,_ b: Int64)->Int64? { let r=a.multipliedReportingOverflow(by:b);return r.overflow ? nil:r.partialValue }
    func combine(_ op: String,_ a: ShaderBounds,_ b: ShaderBounds)->ShaderBounds {
        if ["||","&&","==","!=","<",">","<=",">="].contains(op) { return .boolean }
        if op == "+" { return ShaderBounds(low:both(a.low,b.low,plus),high:both(a.high,b.high,plus)) }
        if op == "-" { return ShaderBounds(low:both(a.low,b.high,minus),high:both(a.high,b.low,minus)) }
        if op == "*", let al=a.low,let ah=a.high,let bl=b.low,let bh=b.high {
            let products=[multiply(al,bl),multiply(al,bh),multiply(ah,bl),multiply(ah,bh)]
            if products.contains(where:{$0 == nil}) { return .unknown }
            return ShaderBounds(low:products.compactMap{$0}.min(),high:products.compactMap{$0}.max())
        }
        if ["/",">>","<<"].contains(op),let al=a.low,let ah=a.high,let bl=b.low,let bh=b.high,bl==bh,bl>=0 {
            if op == "/" { return al>=0 && bl>0 ? ShaderBounds(low:al/bl,high:ah/bl):.unknown }
            guard bl<63 else { return .unknown }
            if op == ">>" { return ShaderBounds(low:al >> bl,high:ah >> bl) }
            return ShaderBounds(low:multiply(al,1 << bl),high:multiply(ah,1 << bl))
        }
        if op == "%",let bl=b.low,let bh=b.high,bl==bh,bl>0 { return ShaderBounds(low:a.low != nil && a.low!>=0 ? 0:-(bl-1),high:bl-1) }
        if op == "&" {
            let caps=[a,b].compactMap { x -> Int64? in guard let l=x.low,let h=x.high,l>=0 else{return nil};return h }
            return caps.isEmpty ? .unknown:ShaderBounds(low:0,high:caps.min())
        }
        if ["|","^"].contains(op),let al=a.low,let ah=a.high,let bl=b.low,let bh=b.high,al>=0,bl>=0 {
            let n=max(ah,bh), bits=64-n.leadingZeroBitCount
            guard bits<63 else{return .unknown};return ShaderBounds(low:0,high:(Int64(1)<<bits)-1)
        }
        return .unknown
    }
    func call(_ name: String,_ args: [ShaderBounds])->ShaderBounds {
        if name == "as_type_u" && args.count == 1 {return ShaderBounds(low:0,high:nil)}
        if ["int","uint","short","ushort","long","ulong"].contains(name) && args.count == 1 {
            let a=args[0];return name.hasPrefix("u") && (a.low == nil || a.low!<0) ? ShaderBounds(low:0,high:nil):a
        }
        if name == "min" && args.count == 2 {return ShaderBounds(low:both(args[0].low,args[1].low,min),high:[args[0].high,args[1].high].compactMap{$0}.min())}
        if name == "max" && args.count == 2 {return ShaderBounds(low:[args[0].low,args[1].low].compactMap{$0}.max(),high:both(args[0].high,args[1].high,max))}
        if name == "clamp" && args.count == 3 {return ShaderBounds(low:args[1].low,high:args[2].high)}
        return .unknown
    }
}

enum ShaderBufferAnalysis {
    static func definitions(_ source: String)->[String:(String,String)] {
        let assignment = #"(?<![\w.])(_\d+)\s*(?:<<|>>|[-+*/%&|^])?=(?!=)|(?<![\w.])(_\d+)\s*(?:\+\+|--)|(?:\+\+|--)\s*(_\d+)\b"#
        var writes:[String:Int]=[:],counts:[String:Int]=[:],decls:[String:Set<String>]=[:],values:[String:(String,String)]=[:]
        for m in matches(assignment,source) {let name=(1...3).map{m[$0]}.first{!$0.isEmpty}!;writes[name,default:0]+=1}
        for m in matches(#"\b(u?int|u?short|u?long|bool)\s+(_\d+)\s*=\s*([^;]+);"#,source) {
            let name=m[2],text=trim(m[3]);counts[name,default:0]+=1;decls[name,default:[]].insert(m[1]+"\0"+text);values[name]=(m[1],text)
        }
        return values.filter{decls[$0.key]!.count == 1 && writes[$0.key,default:0] == counts[$0.key]}
    }
    static func c1Unproven(_ source:String,bytes:Int)->Bool {
        let accesses=Set(matches(#"\b[a-z]+_c1\)\._m0\[([^\]]+)\]"#,source).map{$0[1]})
        if accesses.isEmpty{return false}
        let defs=definitions(source),span=Int64((bytes+255)/256*256+256)
        for index in accesses {let b=ShaderInterval(defs).expression(index);guard let l=b.low,let h=b.high,l>=0,h<span/16 else{return true}}
        return false
    }
    static func extents(_ source:String,bindings:[[String:Any]])->[Int:Int] {
        var output:[Int:Int]=[:],defs:[String:(String,String)]?
        for b in bindings where b["kind"] as? String == "cbuf" {
            guard let reg=b["reg"] as? Int,let name=b["msl_name"] as? String else{continue}
            let reads=matches("\\(\\*spvDescriptorSet0\\.\(escaped(name))\\)\\._m0\\[([^\\]]+)\\]",source).map{$0[1]}
            let uses=matches("\\b\(escaped(name))\\b",source).count,declared=matches("\\b\(escaped(name))\\s*\\[\\[id\\(",source).count
            if uses != reads.count+declared{continue}
            var high:Int64? = -1
            for index in Set(reads) {
                if defs == nil{defs=definitions(source)}
                let bound=ShaderInterval(defs!).expression(index)
                guard let low=bound.low,let top=bound.high,low>=0 else{high=nil;break}
                high=max(high!,top)
            }
            if let high,high<4095{output[reg]=Int(16*(high+1))}
        }
        return output
    }
    static func unwritten(_ source:String,bindings:[[String:Any]])->[Int] {
        var out=Set<Int>()
        for b in bindings where b["kind"] as? String == "sbuf" {
            guard let reg=b["reg"] as? Int,let member=b["msl_name"] as? String else{continue}
            let name="spvDescriptorSet0."+member,ns=source as NSString
            var ok=true
            let reads=matches("\\(\\*\(escaped(name))\\b",source)
            for m in reads {
                var j=m.end
                if (m.start>0 && ns.character(at:m.start-1)==38) || !slice(source,j).hasPrefix(")._m0["){ok=false;break}
                j += ")._m0".count;var depth=0
                while j<ns.length {
                    let c=ns.character(at:j)
                    if c==91{depth+=1}else if c==93{depth-=1}
                    else if depth==0 && !(c==46 || c==95 || (48...57).contains(c) || (65...90).contains(c) || (97...122).contains(c)){break}
                    j+=1
                }
                if match(#"^\s*(?:=(?!=)|[-+*/%&|^]=|<<=|>>=|\+\+|--)"#,slice(source,j)) != nil{ok=false;break}
            }
            if ok && matches("\(escaped(name))\\b",source).count==reads.count{out.insert(reg)}
        }
        return out.sorted()
    }
}
