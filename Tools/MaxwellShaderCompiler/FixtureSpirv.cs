using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

// This is deliberately NOT a general SPIR-V validator/optimizer. It admits only
// the original parameter-add-store fixture's scalar computation and bounded
// declarations. Numeric opcodes/decorations follow the installed SPIR-V grammar,
// independently of Ryujinx's IR. External spirv-val is also run by host tests.
// F32/V4 declarations are required by the translator's uniform-buffer layout;
// F32 values may only be loaded then bitcast, never computed as floats.
internal static unsafe class FixtureSpirv {
    private sealed class Inst {
        internal readonly uint Op;
        internal readonly uint[] A;
        internal readonly int WordOffset, Words;
        internal Inst(uint op, uint[] a, int offset, int words) { Op=op; A=a; WordOffset=offset; Words=words; }
    }
    private static void Need(bool condition) { if (!condition) throw new FixtureCompiler.Unsupported(); }
    internal static byte[] Normalize(byte[] source, FixtureCompiler.Input state,
        out FixtureCompiler.Reflection reflection) {
        reflection=default;
        Need(source != null && source.Length >= 20 && source.Length <= 16384 && (source.Length & 3)==0);
        uint[] words=new uint[source.Length/4];
        for(int i=0;i<words.Length;i++) words[i]=BinaryPrimitives.ReadUInt32LittleEndian(source.AsSpan(i*4,4));
        Need(words[0]==0x07230203 && words[1]==0x00010300 && words[2]==0 && words[3] is > 1 and <= 128 && words[4]==0);
        uint bound=words[3];
        var insts=new List<Inst>(128);
        var defs=new Inst[bound];
        var refs=new List<uint>(256);
        var caps=new HashSet<uint>();
        var names=new Dictionary<uint,string>();
        var decorations=new Dictionary<(uint,uint),uint>();
        var offsets=new Dictionary<(uint,uint),uint>();
        uint entry=0, modeEntry=0; int memoryModels=0, functions=0, labels=0, returns=0, ends=0;
        int loads=0, stores=0, adds=0, casts=0, accesses=0;
        int phase=0; // capabilities, model/entry, debug/decorations, declarations, function, closed
        void Ref(uint id) { Need(id>0 && id<bound); refs.Add(id); }
        void Def(Inst ins,int at) { uint id=ins.A[at]; Need(id>0 && id<bound && defs[id]==null); defs[id]=ins; }
        void Count(Inst ins,int n) => Need(ins.A.Length==n);
        string Text(Inst ins,int at) {
            Need(ins.A.Length>at && ins.A.Length-at<=8);
            byte[] bytes=new byte[(ins.A.Length-at)*4];
            for(int k=at;k<ins.A.Length;k++) BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan((k-at)*4,4),ins.A[k]);
            int zero=Array.IndexOf(bytes,(byte)0); Need(zero>=0);
            for(int k=0;k<zero;k++) Need(bytes[k]>=32 && bytes[k]<=126);
            for(int k=zero;k<bytes.Length;k++) Need(bytes[k]==0);
            return Encoding.ASCII.GetString(bytes,0,zero);
        }
        for(int pos=5;pos<words.Length;) {
            int count=(int)(words[pos]>>16); uint op=words[pos]&65535;
            Need(count>=1 && count<=16 && count<=words.Length-pos && insts.Count<128 && phase!=5);
            var a=words.AsSpan(pos+1,count-1).ToArray(); var ins=new Inst(op,a,pos,count); insts.Add(ins);
            switch(op) {
                case 17: // Capability: exact known generator set; no float64 or unknown capability.
                    Count(ins,1); Need(phase==0 && a[0] is 1 or 64 or 65 or 62 or 47 or 25 or 50 or 46 && caps.Add(a[0])); break;
                case 14:
                    Count(ins,2); Need(phase==0 && a[0]==0 && a[1]==1); phase=1; memoryModels++; break;
                case 15:
                    Count(ins,4); Need(phase==1 && entry==0 && a[0]==5 && Text(ins,2)=="main"); entry=a[1];Ref(entry);break;
                case 16:
                    Count(ins,5); Need(phase==1 && modeEntry==0 && a[1]==17 && a[2]==1 && a[3]==1 && a[4]==1);
                    modeEntry=a[0];Ref(modeEntry);break;
                case 5:
                    Need(phase is 1 or 2 && a.Length>=2);phase=2;Ref(a[0]);Need(names.TryAdd(a[0],Text(ins,1)));break;
                case 71:
                    Need(phase is 1 or 2 && a.Length>=2);phase=2;Ref(a[0]);
                    Need(a[1] is 2 or 3 or 6 or 33 or 34);Count(ins,a[1] is 2 or 3?2:3);
                    Need(decorations.TryAdd((a[0],a[1]),a.Length==2?0:a[2]));break;
                case 72:
                    Count(ins,4);Need(phase is 1 or 2 && a[2]==35 && a[1]<8 && a[3]<=65536);phase=2;Ref(a[0]);
                    Need(offsets.TryAdd((a[0],a[1]),a[3]));break;
                case 19: case 21: case 22: case 23: case 28: case 29: case 30: case 32: case 33:
                    Need(phase is >=1 and <=3);phase=3;
                    int required=op switch{19=>1,21=>3,22=>2,23=>3,28=>3,29=>2,32=>3,33=>2,_=>-1};
                    if(required>=0)Count(ins,required);else Need(a.Length is >=2 and <=9);
                    Def(ins,0);
                    switch(op){
                        case 21:Need(a[1]==32 && a[2]<=1);break;
                        case 22:Need(a[1]==32);break;
                        case 23:Ref(a[1]);Need(a[2]==4);break;
                        case 28:Ref(a[1]);Ref(a[2]);break;
                        case 29:case 33:Ref(a[1]);break;
                        case 30:for(int k=1;k<a.Length;k++)Ref(a[k]);break;
                        case 32:Need(a[1]==2);Ref(a[2]);break; // Uniform only, BufferBlock identifies SSBO.
                    }break;
                case 43:
                    Count(ins,3);Need(phase==3 && a[2]<=4096);Ref(a[0]);Def(ins,1);break;
                case 59:
                    Count(ins,3);Need(phase==3 && a[2]==2);Ref(a[0]);Def(ins,1);break;
                case 54:
                    Count(ins,4);Need(phase==3 && a[2]==0);phase=4;Ref(a[0]);Def(ins,1);Ref(a[3]);functions++;break;
                case 248:
                    Count(ins,1);Need(phase==4 && labels==0);Def(ins,0);labels++;break;
                case 65:
                    Need(phase==4 && labels==1 && returns==0 && a.Length is 5 or 6);Ref(a[0]);Def(ins,1);
                    for(int k=2;k<a.Length;k++)Ref(a[k]);accesses++;break;
                case 61:case 124:
                    Count(ins,3);Need(phase==4 && labels==1 && returns==0);Ref(a[0]);Def(ins,1);Ref(a[2]);
                    if(op==61)loads++;else casts++;break;
                case 128:
                    Count(ins,4);Need(phase==4 && labels==1 && returns==0);Ref(a[0]);Def(ins,1);Ref(a[2]);Ref(a[3]);adds++;break;
                case 62:
                    Count(ins,2);Need(phase==4 && labels==1 && returns==0);Ref(a[0]);Ref(a[1]);stores++;break;
                case 253:Count(ins,0);Need(phase==4 && labels==1 && returns==0);returns++;break;
                case 56:Count(ins,0);Need(phase==4 && returns==1);phase=5;ends++;break;
                default:throw new FixtureCompiler.Unsupported();
            }
            pos+=count;
        }
        Need(memoryModels==1 && entry==modeEntry && functions==1 && labels==1 && returns==1 && ends==1 &&
            accesses==3 && loads==2 && casts==3 && adds==1 && stores==1 && caps.SetEquals(new uint[]{1,64,65,62,47,25,50,46}));
        foreach(uint id in refs)Need(defs[id]!=null);
        Inst D(uint id,uint op){Need(id>0 && id<bound && defs[id]!=null && defs[id].Op==op);return defs[id];}
        bool Scalar(uint id) => defs[id].Op is 21 or 22;
        uint Const(uint id){var d=D(id,43);D(d.A[0],21);return d.A[2];}
        uint Deco(uint id,uint kind){Need(decorations.TryGetValue((id,kind),out uint value));return value;}
        uint Member(uint id,uint member){Need(offsets.TryGetValue((id,member),out uint value));return value;}
        // Validate every declaration, including unused support metadata. No image,
        // subgroup, 64-bit, variable pointer, workgroup or executable extension path exists.
        int variables=0;
        foreach(var d in defs){if(d==null)continue;var a=d.A;
            switch(d.Op){
                case 23:Need(Scalar(a[1]));break;
                case 28:Need(defs[a[1]].Op is 21 or 22 or 23);Need(Const(a[2]) is >0 and <=4096);Need(Deco(a[0],6)==16);break;
                case 29:Need(D(a[1],21).A[2]==0 && Deco(a[0],6)==4);break;
                case 30:for(int j=1;j<a.Length;j++){Need(defs[a[j]].Op is 21 or 22 or 23 or 28 or 29);Member(a[0],(uint)j-1);}break;
                case 32:Need(defs[a[2]].Op is 21 or 22 or 30);break;
                case 33:D(a[1],19);break;
                case 43:D(a[0],21);break;
                case 59:{var pt=D(a[0],32);D(pt.A[2],30);Need(Deco(a[1],34)==0 && Deco(a[1],33)<=7);variables++;break;}
            }
        }
        Need(variables==3 && names.Count==3);
        uint Named(string name){uint result=0;foreach(var pair in names)if(pair.Value==name){Need(result==0);result=pair.Key;}Need(result!=0);D(result,59);return result;}
        uint support=Named("support_buffer"),uniform=Named("cp_c3"),storage=Named("cp_s0");
        Need(Deco(support,33)==0 && Deco(uniform,33)==state.UniformBinding && Deco(storage,33)==state.StorageBinding);
        uint Struct(uint v) => D(D(v,59).A[0],32).A[2];
        uint st=Struct(support),ut=Struct(uniform),bt=Struct(storage);
        Need(decorations.ContainsKey((st,2)) && decorations.ContainsKey((ut,2)) && decorations.ContainsKey((bt,3)) &&
            !decorations.ContainsKey((st,3)) && !decorations.ContainsKey((ut,3)) && !decorations.ContainsKey((bt,2)));
        Need(D(st,30).A.Length==9 && D(ut,30).A.Length==2 && D(bt,30).A.Length==2);
        uint[] supportOffsets={0,16,144,160,176,192,1360,1376};
        for(uint j=0;j<8;j++)Need(Member(st,j)==supportOffsets[j]);
        Need(Member(ut,0)==0 && Member(bt,0)==0);
        var ua=D(D(ut,30).A[1],28);Need(Const(ua.A[2])==4096 && D(D(ua.A[1],23).A[1],22).A[1]==32);
        D(D(bt,30).A[1],29);
        // Every decoration must apply to the correct declaration kind; unused or
        // duplicate binding/type annotations cannot hide an extra resource.
        foreach(var pair in decorations){uint id=pair.Key.Item1,kind=pair.Key.Item2;
            if(kind is 2 or 3)D(id,30);
            else if(kind==6)Need(defs[id].Op is 28 or 29);
            else D(id,59);
        }
        foreach(var pair in offsets){var t=D(pair.Key.Item1,30);Need(pair.Key.Item2<t.A.Length-1);}
        var fn=D(entry,54);D(fn.A[0],19);Need(D(fn.A[3],33).A[1]==fn.A[0]);
        // Symbolically evaluate the straight-line body. Address chains use only
        // literal indices, and memory accesses must be CB[12], CB[16], SSBO[0].
        var pointers=new Dictionary<uint,(uint Variable,uint Offset,uint Element)>();
        var values=new Dictionary<uint,(uint Type,uint Expression)>();
        uint expression=0;bool wrote=false;
        foreach(var ins in insts){var a=ins.A;
            if(ins.Op==65){
                uint variable=a[2];Need(variable==uniform || variable==storage);uint type=Struct(variable),offset=0;
                for(int j=3;j<a.Length;j++){
                    uint index=Const(a[j]);var t=defs[type];
                    if(t.Op==30){Need(index<t.A.Length-1);offset=checked(offset+Member(type,index));type=t.A[index+1];}
                    else if(t.Op is 28 or 29){if(t.Op==28)Need(index<Const(t.A[2]));else Need(index<state.StorageSize/4);
                        offset=checked(offset+index*Deco(type,6));type=t.A[1];}
                    else if(t.Op==23){Need(index<t.A[2]);offset=checked(offset+index*4);type=t.A[1];}
                    else throw new FixtureCompiler.Unsupported();
                }
                Need(Scalar(type) && D(a[0],32).A[2]==type &&
                    ((variable==uniform && offset is 12 or 16 && defs[type].Op==22) ||
                     (variable==storage && offset==0 && D(type,21).A[2]==0)));
                Need(pointers.TryAdd(a[1],(variable,offset,type)));
            }else if(ins.Op==61){
                Need(pointers.TryGetValue(a[2],out var p) && p.Variable==uniform && p.Element==a[0]);
                uint expr=p.Offset==12?1u:2u;Need((expression&expr)==0);expression|=expr;
                Need(values.TryAdd(a[1],(a[0],expr)));
            }else if(ins.Op==124){
                Need(values.TryGetValue(a[2],out var value) && Scalar(a[0]) && Scalar(value.Type));
                Need(defs[a[0]].Op==21 && (defs[value.Type].Op==22 || defs[value.Type].Op==21));
                Need(values.TryAdd(a[1],(a[0],value.Expression)));
            }else if(ins.Op==128){
                Need(values.TryGetValue(a[2],out var left));Need(values.TryGetValue(a[3],out var right));
                Need(left.Type==a[0] && right.Type==a[0] && D(a[0],21).A[2]==1 &&
                    ((left.Expression==1 && right.Expression==2)||(left.Expression==2 && right.Expression==1)));
                Need(values.TryAdd(a[1],(a[0],3)));
            }else if(ins.Op==62){
                Need(pointers.TryGetValue(a[0],out var p) && p.Variable==storage && p.Offset==0 &&
                    values.TryGetValue(a[1],out var value) && value.Type==p.Element && value.Expression==3 && !wrote);
                wrote=true;
            }
        }
        Need(wrote && expression==3);
        // Proven scalar subset has no use for the seven unconditional declarations.
        // Remove declarations only; instruction/type/layout semantics are unchanged.
        var output=new List<uint>(words.Length);output.AddRange(words.AsSpan(0,5).ToArray());
        foreach(var ins in insts)if(ins.Op!=17 || ins.A[0]==1)
            for(int j=0;j<ins.Words;j++)output.Add(words[ins.WordOffset+j]);
        byte[] result=new byte[output.Count*4];
        for(int j=0;j<output.Count;j++)BinaryPrimitives.WriteUInt32LittleEndian(result.AsSpan(j*4,4),output[j]);
        reflection=new FixtureCompiler.Reflection{
            Version=1,StructSize=(uint)sizeof(FixtureCompiler.Reflection),SpirvSize=(uint)result.Length,
            BufferCount=3,ActiveCount=2,X=1,Y=1,Z=1,Capability=1,
            Support=new FixtureCompiler.Buffer{Kind=1,Set=0,Binding=0,Slot=uint.MaxValue},
            Uniform=new FixtureCompiler.Buffer{Kind=1,Set=0,Binding=state.UniformBinding,Slot=3,Flags=1,RequiredBytes=32},
            Storage=new FixtureCompiler.Buffer{Kind=2,Set=0,Binding=state.StorageBinding,Slot=0,AddressCbSlot=3,AddressCbOffset=0,Flags=3,RequiredBytes=4}
        };
        return result;
    }
}
