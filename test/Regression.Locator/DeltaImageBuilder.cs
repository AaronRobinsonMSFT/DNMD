using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Emit;
using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Regression.Locator
{
    internal static class DeltaImageBuilder
    {
        public static DeltaAssembly CreateAssembly()
        {
            Compilation baselineCompilation = CSharpCompilation.Create("DeltaAssembly1")
                .WithReferences(Basic.Reference.Assemblies.NetStandard20.ReferenceInfos.netstandard.Reference)
                .WithOptions(new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
            SyntaxTree sourceBase = CSharpSyntaxTree.ParseText("""
                using System;
                public class Class1
                {
                    private int field;
                    public void Method(int x)
                    {
                    }

                    public int Property { get; set; }

                    public event EventHandler? Event;
                }
                """);
            baselineCompilation = baselineCompilation.AddSyntaxTrees(
                sourceBase,
                CSharpSyntaxTree.ParseText("""
                using System;
                public class Class2
                {
                    private int field;
                    public void Method(int x)
                    {
                    }

                    public int Property { get; set; }

                    public event EventHandler? Event;
                }
                """));

            Compilation diffCompilation = baselineCompilation.ReplaceSyntaxTree(
                sourceBase,
                CSharpSyntaxTree.ParseText("""
                using System;
                public class Class1
                {
                    private class Attr : Attribute { }

                    private short field2;
                    private int field;

                    [return:Attr]
                    public void Method(int x)
                    {
                    }

                    public int Property { get; set; }

                    public short Property2 { get; set; }

                    public event EventHandler? Event;

                    public event EventHandler? Event2;
                }
                """));

            var diagnostics = baselineCompilation.GetDiagnostics();
            MemoryStream baselineImage = new();
            baselineCompilation.Emit(baselineImage, options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));
            baselineImage.Seek(0, SeekOrigin.Begin);

            ModuleMetadata metadata = ModuleMetadata.CreateFromStream(baselineImage);
            EmitBaseline baseline = EmitBaseline.CreateInitialBaseline(metadata, _ => default, _ => default, true);

            MemoryStream mddiffStream = new();

            diffCompilation.EmitDifference(
                baseline,
                new[]
                {
                CreateSemanticEdit(SemanticEditKind.Update, baselineCompilation, diffCompilation, c => c.GetTypeByMetadataName("Class1")),
                CreateSemanticEdit(SemanticEditKind.Insert, baselineCompilation, diffCompilation, c => c.GetTypeByMetadataName("Class1")!.GetMembers("field2").FirstOrDefault()),
                CreateSemanticEdit(SemanticEditKind.Insert, baselineCompilation, diffCompilation, c => c.GetTypeByMetadataName("Class1")!.GetMembers("Property2").FirstOrDefault()),
                CreateSemanticEdit(SemanticEditKind.Insert, baselineCompilation, diffCompilation, c => c.GetTypeByMetadataName("Class1")!.GetMembers("Event2").FirstOrDefault()),
                CreateSemanticEdit(SemanticEditKind.Update, baselineCompilation, diffCompilation, c => c.GetTypeByMetadataName("Class1")!.GetMembers("Method").FirstOrDefault()),
                CreateSemanticEdit(SemanticEditKind.Insert, baselineCompilation, diffCompilation, c => c.GetTypeByMetadataName("Class1")!.GetTypeMembers("Attr").FirstOrDefault()),
                },
                s =>
                {
                    return false;
                },
                mddiffStream,
                new MemoryStream(), // il stream
                new MemoryStream() // pdb diff stream
            );

            baselineImage.Seek(0, SeekOrigin.Begin);
            return new DeltaAssembly(
                baselineImage.ToArray(),
                [mddiffStream.ToArray()]
            );
        }

        static SemanticEdit CreateSemanticEdit(SemanticEditKind editKind, Compilation baseline, Compilation diff, Func<Compilation, ISymbol?> findSymbol)
        {
            return new SemanticEdit(editKind, findSymbol(baseline), findSymbol(diff));
        }

        public record struct DeltaAssembly(byte[] BaseImage, ImmutableArray<byte[]> MetadataDeltas);
    }
}
