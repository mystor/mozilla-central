#include "MozURL.h"

using namespace mozilla;
using namespace mozilla::net;

namespace {

/// This is the implementation of the nsIURIMutator for MozURL. This type cannot
/// be implemented in Rust code, as it uses types and calling conventions which
/// are not supported by the xpcom-rust bindings.
class MozURLIMutator : public nsIURIMutator {
public:
  NS_DECL_NSIURIMUTATOR
  NS_DECL_NSIURISETTERS
  NS_DECL_NSIURISETSPEC
  NS_DECL_ISUPPORTS

  MozURLIMutator(MozURL* aUrl) {
    mozurl_clone(aUrl, getter_AddRefs(mUrl));
  }
private:
  RefPtr<MozURL> mUrl;
};

NS_IMPL_ISUPPORTS(MozURLIMutator,
                  nsIURIMutator,
                  nsIURISetters,
                  nsIURISetSpec)

// nsIURIMutator
NS_IMETHODIMP
MozURLIMutator::Read(nsIObjectInputStream* aInputStream)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
MozURLIMutator::Deserialize(const URIParams& aParams)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

#define IMUTATOR_PREAMBLE() \
  do { \
    if (aMutator) { \
      NS_ADDREF(*aMutator = this); \
    } \
    if (!mUrl) { \
      return NS_ERROR_NULL_POINTER: \
    } \
  } while(0)

// nsIURISetters
NS_IMETHODIMP
MozURLIMutator::SetScheme(const nsACString& aScheme,
                          nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_scheme(mUrl, &aScheme);
}

NS_IMETHODIMP
MozURLIMutator::SetUserPass(const nsACString& aUserPass,
                            nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_userpass(mUrl, &aUserPass);
}

NS_IMETHODIMP
MozURLIMutator::SetUsername(const nsACString& aUser,
                            nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_user(mUrl, &aUser);
}

NS_IMETHODIMP
MozURLIMutator::SetPassword(const nsACString& aPassword,
                            nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_password(mUrl, &aPassword);
}

NS_IMETHODIMP
MozURLIMutator::SetHostPort(const nsACString& aHostPort,
                            nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_host_port(mUrl, &aHostPort);
}

NS_IMETHODIMP
MozURLIMutator::SetHost(const nsACString& aHost,
                        nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_host(mUrl, &aHost);
}

NS_IMETHODIMP
MozURLIMutator::SetPort(int32_t aPort,
                        nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_port_no(mUrl, aPort);
}

NS_IMETHODIMP
MozURLIMutator::SetPathQueryRef(const nsACString& aPathQueryRef,
                                nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_path_query_ref(mUrl, aPathQueryRef);
}

NS_IMETHODIMP
MozURLIMutator::SetRef(const nsACString& aRef,
                       nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_fragment(mUrl, &aRef);
}

NS_IMETHODIMP
MozURLIMutator::SetFilePath(const nsACString& aFilePath,
                            nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_filepath(mUrl, &aFilePath);
}

NS_IMETHODIMP
MozURLIMutator::SetQuery(const nsACString& aQuery,
                         nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_query(mUrl, &aQuery);
}

NS_IMETHODIMP
MozURLIMutator::SetQueryWithEncoding(const nsACString& aQuery,
                                     const Encoding* aEncoding,
                                     nsIURIMutator** aMutator)
{
  if (aEncoding && aEncoding != UTF_8_ENCODING) {
    return NS_ERROR_NOT_SUPPORTED; // Can't handle other encodings yet :'-(
  }
  return SetQuery(aQuery);
}

NS_IMETHODIMP
MozURLIMutator::SetSpec(const nsACString& aSpec,
                        nsIURIMutator** aMutator)
{
  IMUTATOR_PREAMBLE();
  return mozurl_set_spec(mUrl, &aSpec);
}

nsresult
MozURLIMutator::Finalize(nsIURI** aUri)
{
  mUrl.forget(aUri);
}

} // anonymous namespace


extern "C" {

void
mozurl_get_imutator(MozURL* aUrl, nsIURIMutator** aMutator)
{
  nsCOMPtr<nsIURIMutator> mut = new MozURLIMutator(aUrl);
  mut.forget(aMutator);
}

} // extern "C"
