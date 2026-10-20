#include <expat.h>
#include <histedit.h>
#include <lz4.h>
#include <openssl/evp.h>
#include <rapidjson/document.h>
#include <re2/re2.h>
#include <sql.h>
#include <oneapi/tbb/parallel_for.h>

int main ()
{
  XML_Parser parser = XML_ParserCreate (nullptr);
  if (parser == nullptr)
    {
      return 1;
    }
  bool xml_ok = XML_Parse (parser, "<dep/>", 6, XML_TRUE) == XML_STATUS_OK;
  XML_ParserFree (parser);
  History *history = history_init ();
  if (history == nullptr)
    {
      return 2;
    }
  history_end (history);
  EVP_MD_CTX *digest = EVP_MD_CTX_new ();
  if (digest == nullptr)
    {
      return 3;
    }
  bool crypto_ok = EVP_DigestInit_ex (digest, EVP_sha256 (), nullptr) == 1;
  EVP_MD_CTX_free (digest);
  SQLHENV environment = SQL_NULL_HENV;
  SQLRETURN status = SQLAllocHandle (SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment);
  if (!SQL_SUCCEEDED (status))
    {
      return 4;
    }
  SQLFreeHandle (SQL_HANDLE_ENV, environment);
  rapidjson::Document document;
  document.Parse ("{\"dependency\":true}");
  bool re2_ok = RE2::FullMatch ("prefix", "pre.*");
  oneapi::tbb::parallel_for (0, 4, [] (int) {});
  return xml_ok && crypto_ok && re2_ok && !document.HasParseError ()
	 && document["dependency"].GetBool () && LZ4_versionNumber () > 0 ? 0 : 5;
}
