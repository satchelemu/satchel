using System;
using System.Buffers.Binary;
using System.Collections.Generic;
namespace SatchelProfile {
 internal readonly record struct SpirvFacts(int Words,int Instructions,int Bound,int DeclaredUniforms,int ActiveUniforms);
 // This is a capability/resource proof for the small emitted profile, not the
 // renderer's independent complete entry/interface/type schema admission.
 internal static class SpirvProof {
  internal static SpirvFacts Prove(ReadOnlySpan<byte> bytes) {
   if(bytes.Length<20 || bytes.Length>65536 || bytes.Length%4!=0)throw new NotSupportedException();
   uint[] w=new uint[bytes.Length/4];for(int i=0;i<w.Length;i++){ global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); w[i]=BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(i*4,4));}
   if(w[0]!=0x07230203 || w[1]!=0x00010300 || w[3]==0 || w[3]>32768 || w[4]!=0)throw new NotSupportedException();
   var typeKind=new uint[w[3]];var element=new uint[w[3]];var storage=new uint[w[3]];var root=new uint[w[3]];var valueType=new uint[w[3]];var defined=new bool[w[3]];
   int caps=0,uniforms=0,count=0;bool memory=false,entry=false,inFunction=false,seenFunction=false,label=false,returned=false,closed=false;uint entryFunction=0;var interfaces=new List<uint>();
   bool Valid(uint id)=>id>0 && id<w[3];
   void Define(uint id,uint kind){if(!Valid(id)||defined[id])throw new NotSupportedException();defined[id]=true;typeKind[id]=kind;}
   for(int at=5;at<w.Length;) { global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); 
    int n=(int)(w[at]>>16);uint op=w[at]&65535;if(n==0 || n>w.Length-at || ++count>4096)throw new NotSupportedException();
    uint A(int i){if(i>=n)throw new NotSupportedException();return w[at+i];}
    void Exact(int expected){if(n!=expected)throw new NotSupportedException();}
    if(closed || (inFunction && op!=65 && op!=61 && op!=62 && op!=248 && op!=253 && op!=56))throw new NotSupportedException();
    if(!inFunction && (op==65 || op==61 || op==62 || op==248 || op==253 || op==56))throw new NotSupportedException();
    if(inFunction && returned && op!=56)throw new NotSupportedException();
    if(inFunction && !label && op!=248)throw new NotSupportedException();
    switch(op) {
     case 17:Exact(2);if(A(1)!=1 || ++caps!=1)throw new NotSupportedException();break;
     case 14:Exact(3);if(memory || A(1)!=0 || A(2)!=1)throw new NotSupportedException();memory=true;break;
     case 15:if(entry || n<5 || (A(1)!=0 && A(1)!=4) || !Valid(A(2)) || A(3)!=0x6e69616d || A(4)!=0)throw new NotSupportedException();entry=true;entryFunction=A(2);for(int i=5;i<n;i++){ global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); interfaces.Add(A(i));}break;
     case 16:Exact(3);if(A(2)!=7)throw new NotSupportedException();break;
     case 5:case 6:if(n<3)throw new NotSupportedException();break; // names have no capability effect
     case 71:if(n<3 || (A(2)!=2 && A(2)!=6 && A(2)!=14 && A(2)!=30 && A(2)!=33 && A(2)!=34))throw new NotSupportedException();break;
     case 72:if(n<4 || (A(3)!=11 && A(3)!=35))throw new NotSupportedException();if(A(3)==11 && (n!=5 || A(4)>4 || A(4)==2))throw new NotSupportedException();break;
     case 19:Exact(2);Define(A(1),op);break;
     case 21:Exact(4);if(A(2)!=32 || A(3)>1)throw new NotSupportedException();Define(A(1),op);break;
     case 22:Exact(3);if(A(2)!=32)throw new NotSupportedException();Define(A(1),op);break;
     case 23:Exact(4);if(A(3)!=4 || !Valid(A(2)) || (typeKind[A(2)]!=21 && typeKind[A(2)]!=22))throw new NotSupportedException();Define(A(1),op);element[A(1)]=A(2);break;
     case 28:Exact(4);if(!Valid(A(2)) || !Valid(A(3)) || !defined[A(2)] || !defined[A(3)])throw new NotSupportedException();Define(A(1),op);element[A(1)]=A(2);break;
     case 30:if(n<3 || n>10)throw new NotSupportedException();for(int i=2;i<n;i++){ global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); if(!Valid(A(i)) || !defined[A(i)])throw new NotSupportedException();}Define(A(1),op);break;
     case 32:Exact(4);if(A(2)!=1 && A(2)!=2 && A(2)!=3)throw new NotSupportedException();if(!Valid(A(3)) || !defined[A(3)])throw new NotSupportedException();Define(A(1),op);storage[A(1)]=A(2);element[A(1)]=A(3);break;
     case 33:Exact(3);if(!Valid(A(2)) || typeKind[A(2)]!=19)throw new NotSupportedException();Define(A(1),op);break;
     case 43:Exact(4);if(!Valid(A(1)) || (typeKind[A(1)]!=21 && typeKind[A(1)]!=22))throw new NotSupportedException();Define(A(2),op);valueType[A(2)]=A(1);break;
     case 59:Exact(4);if(!Valid(A(1)) || typeKind[A(1)]!=32 || storage[A(1)]!=A(3))throw new NotSupportedException();Define(A(2),op);root[A(2)]=A(2);storage[A(2)]=A(3);valueType[A(2)]=A(1);if(A(3)==2)uniforms++;break;
     case 65:if(n<5 || n>6 || !Valid(A(1)) || typeKind[A(1)]!=32 || !Valid(A(3)) || root[A(3)]==0)throw new NotSupportedException();
      if(storage[root[A(3)]]==2)throw new NotSupportedException(); // no active support UBO, even address-only use
      for(int i=4;i<n;i++){ global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); if(!Valid(A(i)) || typeKind[A(i)]!=43 || typeKind[valueType[A(i)]]!=21)throw new NotSupportedException();}
      Define(A(2),op);root[A(2)]=root[A(3)];valueType[A(2)]=A(1);break;
     case 61:Exact(4);if(!Valid(A(1)) || typeKind[A(1)]!=22 || !Valid(A(3)) || root[A(3)]==0 || storage[root[A(3)]]!=1 || element[valueType[A(3)]]!=A(1))throw new NotSupportedException();Define(A(2),op);valueType[A(2)]=A(1);break;
     case 62:Exact(3);if(!Valid(A(1)) || root[A(1)]==0 || storage[root[A(1)]]!=3 || !Valid(A(2)) || !defined[A(2)] || element[valueType[A(1)]]!=valueType[A(2)])throw new NotSupportedException();break;
     case 54:Exact(5);if(seenFunction || !memory || !entry || A(2)!=entryFunction || !Valid(A(1)) || typeKind[A(1)]!=19 || A(3)!=0 || !Valid(A(4)) || typeKind[A(4)]!=33)throw new NotSupportedException();Define(A(2),op);seenFunction=true;inFunction=true;break;
     case 248:Exact(2);if(label)throw new NotSupportedException();Define(A(1),op);label=true;break;
     case 253:Exact(1);returned=true;break;
     case 56:Exact(1);if(!returned)throw new NotSupportedException();closed=true;inFunction=false;break;
     default:throw new NotSupportedException();
    }
    at+=n;
   }
   if(caps!=1 || uniforms!=1 || !closed)throw new NotSupportedException();
   foreach(uint id in interfaces){ global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); if(!Valid(id) || typeKind[id]!=59 || (storage[id]!=1 && storage[id]!=3))throw new NotSupportedException();}
   return new SpirvFacts(w.Length,count,(int)w[3],uniforms,0);
  }
 }
}
