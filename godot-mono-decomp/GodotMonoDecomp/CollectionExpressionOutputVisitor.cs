using System.IO;
using ICSharpCode.Decompiler.CSharp;
using ICSharpCode.Decompiler.CSharp.OutputVisitor;
using ICSharpCode.Decompiler.CSharp.Syntax;

namespace GodotMonoDecomp;

internal sealed class CollectionExpressionArrayAnnotation
{
	public static readonly CollectionExpressionArrayAnnotation Instance = new();

	private CollectionExpressionArrayAnnotation()
	{
	}
}

internal sealed class CollectionExpressionSpreadElementAnnotation
{
	public static readonly CollectionExpressionSpreadElementAnnotation Instance = new();

	private CollectionExpressionSpreadElementAnnotation()
	{
	}
}

public class GodotCSharpOutputVisitor : CSharpOutputVisitor
{
	public GodotCSharpOutputVisitor(TextWriter w, CSharpFormattingOptions formattingOptions)
		: base(w, formattingOptions)
	{
	}

	public override void VisitArrayInitializerExpression(ArrayInitializerExpression arrayInitializerExpression)
	{
		if (arrayInitializerExpression.Annotation<CollectionExpressionArrayAnnotation>() == null)
		{
			base.VisitArrayInitializerExpression(arrayInitializerExpression);
			return;
		}

		StartNode(arrayInitializerExpression);
		WriteToken(Roles.LBracket);

		bool first = true;
		foreach (var element in arrayInitializerExpression.Elements)
		{
			if (!first)
			{
				WriteToken(Roles.Comma);
				Space();
			}

			if (element.Annotation<CollectionExpressionSpreadElementAnnotation>() != null)
			{
				WriteToken(BinaryOperatorExpression.RangeRole);
			}

			element.AcceptVisitor(this);
			first = false;
		}

		WriteToken(Roles.RBracket);
		EndNode(arrayInitializerExpression);
	}
}
