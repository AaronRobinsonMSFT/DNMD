namespace Regression.DeltaAssembly
{
    public class Class2
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
}