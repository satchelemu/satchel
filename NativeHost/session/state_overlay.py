"""Register mutable scalar function caches before their first write.

Only plain scalar declarations are accepted. Locks, TLS, object references,
function pointers and allocations require their explicit typed owner.
"""
import re

TOKENS = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]')
# Parentheses belong to a function declarator here. Consuming that signature
# would also consume its first same-line static variable before scope filtering.
DECL = re.compile(r'\bstatic\s+((?:_Atomic\s+)?(?:int(?:8|16|32|64)_t|uint(?:8|16|32|64)_t|int|unsigned|size_t|long|double|float))\s+([^;\n()]+);')


def scalar_caches(text, owner):
    # A declaration inside a brace with a function signature before it is local.
    # Initializer braces are included in the depth but do not open functions.
    spans = []
    depth = 0
    for token in TOKENS.finditer(text):
        if token[0] == '{':
            if depth == 0 and text[:token.start()].rstrip().endswith(')'):
                begin = token.end()
            depth += 1
        elif token[0] == '}':
            depth -= 1
            if depth == 0 and 'begin' in locals():
                spans.append((begin, token.start())); del begin
    edits = []
    for match in DECL.finditer(text):
        scope = next(((lo, hi) for lo, hi in spans if lo <= match.start() < hi), None)
        if scope is None: continue
        if 'dispatch_once(' in text[scope[0]:scope[1]] or 'pthread_once(' in text[scope[0]:scope[1]]: continue
        declaration = match[2]
        # Split commas only outside array initializers. Initializers here must
        # be integer/float constants, never calls, addresses or copied objects.
        parts = re.split(r',\s*(?![^{}]*\})', declaration)
        names = []
        for part in parts:
            var = re.fullmatch(r'\s*([a-zA-Z_]\w*)(?:\[[\w +*<>]+\])*\s*(?:=\s*[-~+\d.{} ,uUlLfFxXaAbBcCdDeE<>]+)?\s*', part)
            if not var: break
            names.append(var[1])
        else:
            if names: edits.append((match.end(), ' ' + ' '.join(f'SATCHEL_STATE({owner}, {n});' for n in names)))
    for position, code in reversed(edits): text = text[:position] + code + text[position:]
    return text


def arc_caches(text):
    # These local caches have no once token. Setting the owner to nil makes
    # their existing lazy initializer build resources for the next session.
    pattern = re.compile(r'\bstatic\s+(?:__strong\s+)?(?:id(?:<[^;{}()\n]+?>)?|NvProg\s*\*|(?:NSMutableDictionary|NSMutableSet|NSMutableArray)<[^;{}()\n]+>\s*\*)\s*([a-zA-Z_]\w*(?:\[\w+\])*(?:,\s*\*?\s*[a-zA-Z_]\w*(?:\[\w+\])*)*);')
    # Runtime capture has a file-global satchel_phone_retired owner too.
    # Register only declarations inside a function body.
    spans=[]; depth=0; begin=None
    for token in TOKENS.finditer(text):
        if token[0]=='{':
            if depth==0 and text[:token.start()].rstrip().endswith(')'):begin=token.end()
            depth+=1
        elif token[0]=='}':
            depth-=1
            if depth==0 and begin is not None:spans.append((begin,token.start()));begin=None
    for match in reversed(list(pattern.finditer(text))):
        scope = next(((lo, hi) for lo, hi in spans if lo <= match.start() < hi), None)
        if scope is None: continue
        if 'dispatch_once(' in text[scope[0]:scope[1]] or 'pthread_once(' in text[scope[0]:scope[1]]: continue
        names = [re.match(r'\w+', p.strip().lstrip('*').strip())[0] for p in match[1].split(',')]
        if any(n.startswith('g_') for n in names): continue
        code = ' ' + ' '.join('SATCHEL_ARC(' + name + ');' for name in names)
        text = text[:match.end()] + code + text[match.end():]
    return text
