using System;
namespace SatchelProfile {
 internal enum BoundKind { Pool,Operation,Operand,Block,CodeFetch,OptimizeVisit,SpirvWords,SpirvId,PassVisit,AstNode }
 internal sealed class BudgetExceeded(BoundKind kind) : Exception { internal readonly BoundKind Kind=kind; }
 internal sealed class BudgetCancelled : Exception { }
 internal static class WorkBudget {
  [ThreadStatic] static bool active;
  [ThreadStatic] internal static Action Poll;
  [ThreadStatic] internal static int TotalLimit;
  internal static int Total=>total;
  internal static void Check(){if(!active)throw new InvalidOperationException();Poll?.Invoke();}
  [ThreadStatic] static int[] remaining,counts;
  [ThreadStatic] static int cancelAfter,total;
  [ThreadStatic] internal static int ConstructedPools;
  internal static int Used=>counts[(int)BoundKind.Pool];
  internal static int[] Counts=>(int[])counts.Clone();
  internal static readonly int[] Maximum={32768,4096,8192,128,128,65536,32768,32768,65536,8192};
  internal static void Begin(int pool,int[] limits=null,int cancel=-1) {
   if(active || pool<0 || pool>Maximum[0] || cancel< -1)throw new InvalidOperationException();
   var candidate=(int[])Maximum.Clone();candidate[0]=pool;
   if(limits!=null){if(limits.Length!=10)throw new InvalidOperationException();for(int i=0;i<limits.Length;i++){if(limits[i]<0 || limits[i]>candidate[i])throw new InvalidOperationException();candidate[i]=limits[i];}}
   remaining=candidate;counts=new int[10];total=0;TotalLimit=65536;Poll=null;cancelAfter=cancel;active=true;
  }
  internal static void End(){Poll=null;active=false;}
  internal static void Charge(BoundKind kind,int amount=1) {
   if(!active || amount<0)throw new InvalidOperationException();Check();
   if(total>=TotalLimit)throw new BudgetExceeded(kind);
   if(cancelAfter>=0 && total>=cancelAfter)throw new BudgetCancelled();
   int index=(int)kind;if(amount>remaining[index])throw new BudgetExceeded(kind);
   total++;remaining[index]-=amount;counts[index]+=amount;
  }
  internal static void ChargePoolItem()=>Charge(BoundKind.Pool);
  internal static void NotePoolConstruction(){ConstructedPools++;}
 }
}
