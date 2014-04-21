using System;
using System.Net;

namespace Cubrid.Data.CubridClient
{
    public class CubridOid
    {
        public const int OID_BYTE_SIZE = 8;

        private byte[] oid;

        public CubridOid(byte[] oid)
        {
            this.oid = oid;
        }

        public byte[] Oid
        {
            get { return this.oid; }
        }

        public override string ToString()
        {
            return "OID:@" + IPAddress.NetworkToHostOrder(BitConverter.ToInt32(oid, 0)) 
                + "|" + IPAddress.NetworkToHostOrder(BitConverter.ToInt16(oid, 4))
                + "|" + IPAddress.NetworkToHostOrder(BitConverter.ToInt16(oid, 6));
        }
    }
}
