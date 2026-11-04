using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Ryujinx.Graphics.Shader;
using Ryujinx.Graphics.Shader.Translation;

// Original feasibility facade: resolves the three linked GLSL
// resources without pulling in unrelated logging, message-pack, or IO packages.
namespace Ryujinx.Common {
    public static class EmbeddedResources {
        public static string ReadAllText(string name) {
            using Stream stream = typeof(EmbeddedResources).Assembly.GetManifestResourceStream(name.Replace('/', '.'))
                ?? throw new InvalidDataException("Embedded shader helper missing.");
            using var reader = new StreamReader(stream);
            return reader.ReadToEnd();
        }
    }
}

