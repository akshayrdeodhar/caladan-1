#include <folly/ExceptionWrapper.h>
#include <folly/String.h>

#include <fizz/protocol/Protocol.h>
#include <fizz/crypto/test/TestUtil.h>
#include <folly/ssl/OpenSSLPtrTypes.h>

#include <list>
#include <iostream>

#include "codec.h"

namespace fizz::test {

folly::ssl::EvpPkeyUniquePtr getPrivateKey(StringPiece key) {
	folly::ssl::BioUniquePtr bio(BIO_new(BIO_s_mem()));
	CHECK(bio);
	CHECK_EQ(BIO_write(bio.get(), key.data(), key.size()), key.size());
	folly::ssl::EvpPkeyUniquePtr pkey(
	PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
	CHECK(pkey);
	return pkey;
}

folly::ssl::X509UniquePtr getCert(folly::StringPiece cert) {
	folly::ssl::BioUniquePtr bio(BIO_new(BIO_s_mem()));
	CHECK(bio);
	CHECK_EQ(BIO_write(bio.get(), cert.data(), cert.size()), cert.size());
	folly::ssl::X509UniquePtr x509(
	PEM_read_bio_X509(
	bio.get(), nullptr, nullptr,
	nullptr));
	CHECK(x509);
	return x509;
}

} // namespace fizz::test

namespace quic {

// Converts the hex encoded string to an IOBuf.
std::unique_ptr<folly::IOBuf>
toIOBuf(std::string hexData, size_t headroom = 0, size_t tailroom = 0) {
	std::string out;
	CHECK(folly::unhexlify(hexData, out));
	return folly::IOBuf::copyBuffer(out, headroom, tailroom);
}

std::pair<std::unique_ptr<Aead>, std::unique_ptr<PacketNumberCipher>>
Ciphers::buildCiphers(folly::ByteRange secret) {
	auto cipher = fizz::CipherSuite::TLS_AES_128_GCM_SHA256;
	auto scheduler = (*state_.context()->getFactory()).makeKeyScheduler(cipher);
	auto aead = FizzAead::wrap(
	fizz::Protocol::deriveRecordAeadWithLabel(
	*state_.context()->getFactory(),
	*scheduler,
	cipher,
	secret,
	kQuicKeyLabel,
	kQuicIVLabel));

	auto out = aead->getFizzAead()->encrypt(
	toIOBuf(folly::hexlify("plaintext")),
	toIOBuf("").get(),
	0);

	std::cout << R"(aead->inplaceEncrypt(hexlify("plaintext"),"",0) = )"
	          << folly::hexlify(out->moveToFbString().toStdString())
						<< std::endl;

	auto headerCipher = cryptoFactory_.makePacketNumberCipher(secret);

	return {std::move(aead), std::move(headerCipher)};
}

std::shared_ptr<fizz::SelfCert> readCert() {
	auto certificate = fizz::test::getCert(fizz::test::kP256Certificate);
	auto privKey = fizz::test::getPrivateKey(fizz::test::kP256Key);
	std::vector<folly::ssl::X509UniquePtr> certs;
	certs.emplace_back(std::move(certificate));
	return std::make_shared<fizz::SelfCertImpl<fizz::KeyType::P256>>(
	std::move(privKey), std::move(certs));
}

void Ciphers::createServerCtx() {
	auto cert = readCert();
	auto certManager = std::make_unique<fizz::server::CertManager>();
	certManager->addCert(std::move(cert), true);
	auto serverCtx = std::make_shared<fizz::server::FizzServerContext>();
	serverCtx->setFactory(std::make_shared<QuicFizzFactory>());
	serverCtx->setCertManager(std::move(certManager));
	serverCtx->setOmitEarlyRecordLayer(true);
	serverCtx->setClock(std::make_shared<fizz::SystemClock>());
	serverCtx->setFactory(cryptoFactory_.getFizzFactory());
	serverCtx->setSupportedCiphers({{fizz::CipherSuite::TLS_AES_128_GCM_SHA256}});
	serverCtx->setVersionFallbackEnabled(false);
	// Since Draft-17, client won't sent EOED
	serverCtx->setOmitEarlyRecordLayer(true);
	state_.context() = std::move(serverCtx);
}

Ciphers::Ciphers() {
	createServerCtx();
}

void Ciphers::computeCiphers(CipherKind kind, folly::ByteRange secret) {
	std::unique_ptr<quic::Aead> aead;
	std::unique_ptr<quic::PacketNumberCipher> headerCipher;
	std::tie(aead, headerCipher) = buildCiphers(secret);
	switch (kind) {
		case CipherKind::HandshakeRead:
			handshakeReadCipher_ = std::move(aead);
			handshakeReadHeaderCipher_ = std::move(headerCipher);
			break;
		case CipherKind::HandshakeWrite:
			handshakeWriteCipher_ = std::move(aead);
			handshakeWriteHeaderCipher_ = std::move(headerCipher);
			break;
		case CipherKind::OneRttRead:
			oneRttReadCipher_ = std::move(aead);
			oneRttReadHeaderCipher_ = std::move(headerCipher);
			break;
		case CipherKind::OneRttWrite:
			oneRttWriteCipher_ = std::move(aead);
			oneRttWriteHeaderCipher_ = std::move(headerCipher);
			break;
		case CipherKind::ZeroRttRead:
			zeroRttReadCipher_ = std::move(aead);
			zeroRttReadHeaderCipher_ = std::move(headerCipher);
			break;
		default:
			folly::assume_unreachable();
	}
}

Ciphers::~Ciphers() = default;

#if 0
MyCipher::MyCipher(std::string key, std::string iv) {
	TrafficKey trafficKey;
	cipher = OpenSSLEVPCipher::makeCipher<AESGCM128>();
	trafficKey.key = IOBuf::copyBuffer(key);
	trafficKey.iv = IOBuf::copyBuffer(iv);
	cipher->setKey(std::move(trafficKey));
}

void dummyfree(void *ptr, void *userdata) {}

// Expect that data buffer = (payload, tail), with last "overhead" bytes free
void *
MyCipher::encrypt(
	void *payload, void *aad, int payload_and_tail, int aadlen,
	uint64_t seqNo) {
	std::unique_ptr<IOBuf> plaintext = IOBuf::takeOwnership(
		payload,
		payload_and_tail,
		dummyfree);
	std::unique_ptr<IOBuf> aadbuf = IOBuf::takeOwnership(aad, aadlen, dummyfree);
	plaintext->trimEnd(cipher->getCipherOverhead());
	auto ciphertext = cipher->inplaceEncrypt(
		std::move(plaintext), aadbuf.get(),
		seqNo);
	return (void *) std::move(ciphertext).get();
}

void *
MyCipher::decrypt(
	void *payload, void *aad, int payload_and_tail, int aadlen,
	uint64_t seqNo) {
	std::unique_ptr<IOBuf> ciphertext = IOBuf::takeOwnership(
		payload,
		payload_and_tail,
		dummyfree);
	std::unique_ptr<IOBuf> aadbuf = IOBuf::takeOwnership(aad, aadlen, dummyfree);
	auto decrypted = cipher->decrypt(std::move(ciphertext), aadbuf.get(), seqNo);
	return (void *) std::move(decrypted).get();
}
#endif

} // namespace quic
