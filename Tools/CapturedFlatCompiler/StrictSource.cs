using System;
using System.Buffers.Binary;
namespace SatchelProfile {
 internal readonly record struct SourceFacts(uint AttributeMask,uint VaryingMask,int Operations);
 internal sealed class AdmittedPair {
  internal readonly byte[] Vertex,Fragment;
  internal readonly SourceFacts VertexFacts,FragmentFacts;
  internal AdmittedPair(ReadOnlySpan<byte> vertex,ReadOnlySpan<byte> fragment) {
   // Bound before copying, then decode only private immutable snapshots.
   StrictSource.Length(vertex.Length);StrictSource.Length(fragment.Length);
   Vertex=vertex.ToArray();Fragment=fragment.ToArray();
   VertexFacts=StrictSource.Decode(Vertex,true);FragmentFacts=StrictSource.Decode(Fragment,false);
   if(VertexFacts.VaryingMask!=FragmentFacts.VaryingMask)throw new NotSupportedException();
  }
 }
 internal static class StrictSource {
  internal static void Length(int n){if(n<=80 || n>1104 || (n&7)!=0)throw new NotSupportedException();}
  static uint U32(ReadOnlySpan<byte> b,int at)=>BinaryPrimitives.ReadUInt32LittleEndian(b.Slice(at,4));
  internal static SourceFacts Decode(ReadOnlySpan<byte> b,bool vertex) {
   Length(b.Length);Span<uint> h=stackalloc uint[20];for(int i=0;i<20;i++){ global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); h[i]=U32(b,i*4);}
   uint expected=(vertex?1u:2u)|(3u<<5)|((vertex?1u:5u)<<10)|(3u<<17);
   if(h[0]!=expected || h[1]!=0 || h[2]!=0 || h[3]!=0 || (h[4]&~0xff0ff000u)!=0)throw new NotSupportedException();
   for(int i=5;i<20;i++) { global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); 
    if(vertex && (i==6 || i==13 || i==14))continue;
    if(!vertex && (i==6 || i==7 || i==18))continue;
    if(h[i]!=0)throw new NotSupportedException();
   }
   uint varyingMask=0;
   for(int location=0;location<8;location++) { global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); 
    uint declaration=vertex?(h[14]>>(location*4))&15u:(h[6+location/4]>>((location%4)*8))&255u;
    if(declaration==0)continue;
    if(declaration!=(vertex?15u:85u) || varyingMask!=0)throw new NotSupportedException();
    varyingMask=1u<<location;
   }
   if(varyingMask==0 || (vertex?h[13]!=0xf0000000u:h[18]!=15))throw new NotSupportedException();
   Span<bool> defined=stackalloc bool[256];defined.Clear();defined[255]=true;
   uint attributes=0,position=0,varyingWritten=0,varyingRead=0;int operations=0;bool exited=false;
   for(int at=80;at<b.Length;at+=8) { global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); 
    ulong op=BinaryPrimitives.ReadUInt64LittleEndian(b.Slice(at,8));if(((at-80)&31)==0)continue;
    if(exited){if(op!=0 && op!=0x50b0000000070f00UL)throw new NotSupportedException();continue;}
    if(++operations>64)throw new NotSupportedException();
    if(op==0xe30000000007000fUL){exited=true;continue;}
    ulong fields=255UL|(0xffffffffUL<<20);
    if((op&~fields)==0x010000000007f000UL){defined[(int)(op&255)]=true;continue;}
    fields=255UL|(255UL<<20);
    if((op&~fields)==0x5c98078000070000UL){if(!defined[(int)((op>>20)&255)])throw new NotSupportedException();defined[(int)(op&255)]=true;continue;}
    fields=255UL|(0x7ffUL<<20)|(3UL<<47);
    int d=(int)(op&255),off=(int)((op>>20)&0x7ff),count=(int)((op>>47)&3)+1;
    if(vertex && (op&~fields)==0xefd87f800007ff00UL) {
     if(off<0x80 || (off&3)!=0 || off+4*count>0x100 || d+count>255)throw new NotSupportedException();
     for(int j=0;j<count;j++){ global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); int c=(off-0x80)/4+j;if((h[6]&(1u<<c))==0)throw new NotSupportedException();attributes|=1u<<(c/4);defined[d+j]=true;}
     continue;
    }
    if(vertex && (op&~fields)==0xeff07f800007ff00UL) {
     if(off<0x70 || (off&3)!=0 || off+4*count>0x100 || d+count>256 || (off<0x80 && off+4*count>0x80))throw new NotSupportedException();
     for(int j=0;j<count;j++) { global::SatchelProfile.WorkBudget.Charge(global::SatchelProfile.BoundKind.PassVisit); 
      if(!defined[d+j])throw new NotSupportedException();int offset=off+4*j;
      if(offset<0x80)position|=1u<<((offset-0x70)/4);
      else {int index=(offset-0x80)/4;if((varyingMask&(1u<<(index/4)))==0)throw new NotSupportedException();varyingWritten|=1u<<(index%4);}
     }
     continue;
    }
    fields=255UL|(0x3ffUL<<28);
    const ulong ipa=0xe000000000000000UL|(7UL<<47)|(255UL<<39)|(255UL<<20)|(7UL<<16)|(255UL<<8);
    if(!vertex && (op&~fields)==ipa) {
     int offset=(int)((op>>28)&0x3ff);
     if(offset<0x80 || offset>=0x100 || (offset&3)!=0 || (varyingMask&(1u<<((offset-0x80)/16)))==0)throw new NotSupportedException();
     varyingRead|=1u<<(((offset-0x80)/4)%4);defined[d]=true;continue;
    }
    throw new NotSupportedException();
   }
   if(!exited || (vertex?(position!=15 || varyingWritten!=15):(!defined[0] || !defined[1] || !defined[2] || !defined[3] || varyingRead!=15)))throw new NotSupportedException();
   return new SourceFacts(attributes,varyingMask,operations);
  }
 }
}
