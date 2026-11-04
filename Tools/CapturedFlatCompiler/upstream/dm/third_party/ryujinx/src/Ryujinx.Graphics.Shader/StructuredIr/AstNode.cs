using System.Collections.Generic;

namespace Ryujinx.Graphics.Shader.StructuredIr
{
    class AstNode : IAstNode
    {
        protected AstNode() { PocketProfile.WorkBudget.Charge(PocketProfile.BoundKind.AstNode); }
        public AstBlock Parent { get; set; }

        public LinkedListNode<IAstNode> LLNode { get; set; }
    }
}
