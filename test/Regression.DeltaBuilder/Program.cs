using System.Collections.Immutable;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Emit;

/// <summary>
/// Generates images with metadata deltas for use in the regression tests.
/// </summary>
public static class Program
{
    /// <summary>
    /// Generate images with metadata deltas into the output directory.
    /// </summary>
    /// <param name="output">The directory to output the base and delta images into.</param>
    public static void Main(DirectoryInfo output)
    {
        // Create the directory if it does not exist.
        output.Create();

        ImmutableArray<DeltaAssembly> imageScenarios = [ DeltaAssembly1() ];

        foreach (DeltaAssembly scenario in imageScenarios)
        {
            File.WriteAllBytes(Path.Combine(output.FullName, $"{scenario.Name}.dll"), scenario.BaseImage);
            Console.WriteLine($"Wrote {scenario.Name}.dll base image");
            for (int i = 0; i < scenario.MetadataDeltas.Length; i++)
            {
                File.WriteAllBytes(Path.Combine(output.FullName, $"{scenario.Name}.{i + 1}.mddelta"), scenario.MetadataDeltas[i]);
                Console.WriteLine($"Wrote {scenario.Name}.dll metadata delta {i + 1}");
            }
        }
    }

    static DeltaAssembly DeltaAssembly1()
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
            nameof(DeltaAssembly1),
            baselineImage.ToArray(),
            [ mddiffStream.ToArray() ]
        );
    }

    static SemanticEdit CreateSemanticEdit(SemanticEditKind editKind, Compilation baseline, Compilation diff, Func<Compilation, ISymbol?> findSymbol)
    {
        return new SemanticEdit(editKind, findSymbol(baseline), findSymbol(diff));
    }

    record struct DeltaAssembly(string Name, byte[] BaseImage, ImmutableArray<byte[]> MetadataDeltas);
}