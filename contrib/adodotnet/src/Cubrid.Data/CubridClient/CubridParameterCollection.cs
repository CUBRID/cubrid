using System;
using System.Data.Common;
using System.Collections;

namespace Cubrid.Data.CubridClient
{
    public sealed class CubridParameterCollection : DbParameterCollection
    {
        private ArrayList param = new ArrayList();

        internal int BindCount
        {
            set
            {
                ;
            }
        }

        public override int Count
        {
            get
            {
                return param.Count;
            }
        }

        public override bool IsFixedSize
        {
            get
            {
                return false;
            }
        }

        public override bool IsReadOnly
        {
            get
            {
                return false;
            }
        }

        public override bool IsSynchronized
        {
            get
            {
                return false;
            }
        }

        public override object SyncRoot
        {
            get
            {
                return null;
            }
        }

        public CubridParameterCollection()
        {
        }

        public CubridParameterCollection(int bindCount)
        {
        }

        public override int Add(object value)
        {
            return param.Add(new CubridParameter(value));
        }

        public int Add()
        {
            return param.Add(new CubridParameter());
        }

        public int Add(CubridDataType type)
        {
            return param.Add(new CubridParameter(type));
        }

        public override void AddRange(Array values)
        {
        }

        public override void Clear()
        {
            param.Clear();
        }

        public override bool Contains(object value)
        {
            return false;
        }

        public override bool Contains(string value)
        {
            return false;
        }

        public override void CopyTo(Array array, int index)
        {
        }

        public override IEnumerator GetEnumerator()
        {
            return param.GetEnumerator();
        }

        protected override DbParameter GetParameter(int index)
        {
            return (DbParameter)param[index];
        }

        protected override DbParameter GetParameter(string parameterName)
        {
            throw new NotImplementedException();
        }

        public override int IndexOf(object value)
        {
            return 0;
        }

        public override int IndexOf(string parameterName)
        {
            throw new NotImplementedException();
        }

        public override void Insert(int index, object value)
        {
            param.Insert(index, new CubridParameter(value));
        }

        public override void Remove(object value)
        {
        }

        public override void RemoveAt(int index)
        {
            param.RemoveAt(index);
        }

        public override void RemoveAt(string parameterName)
        {
            throw new NotImplementedException();
        }

        protected override void SetParameter(int index, DbParameter value)
        {
            param[index] = value;
        }

        protected override void SetParameter(string parameterName, DbParameter value)
        {
            throw new NotImplementedException();
        }
    }
}
