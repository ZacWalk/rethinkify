#pragma once

namespace crypto
{
	class sha1
	{
	public:
		sha1();
		void update(const uint8_t* input, size_t len);
		void final(uint8_t* digest);

		static const unsigned int DIGEST_SIZE = 5 * 4;

	private:
		static const unsigned int DIGEST_INTS = 5; /* number of 32bit integers per SHA1 digest */
		static const unsigned int BLOCK_INTS = 16; /* number of 32bit integers per SHA1 block */
		static const unsigned int BLOCK_BYTES = BLOCK_INTS * 4;


		std::vector<uint8_t> buffer;
		uint64_t transforms;
		uint32_t digest[DIGEST_INTS];

		void reset();
		void transform(uint32_t block[DIGEST_INTS]);

		static void buffer_to_block(const std::vector<uint8_t>& buffer, uint32_t block[BLOCK_BYTES]);
	};

	class sha256
	{
	public:
		sha256();
		void update(const uint8_t* input, size_t len);
		void final(uint8_t* digest);

		static const unsigned int DIGEST_SIZE = (256 / 8);

	private:

		const static uint32_t sha256_k[];
		static const uint32_t SHA224_256_BLOCK_SIZE = (512 / 8);

		void transform(const uint8_t* message, unsigned int block_nb);
		void reset();

		uint32_t m_tot_len;
		uint32_t m_len;
		uint8_t m_block[2 * SHA224_256_BLOCK_SIZE];
		uint32_t m_h[8];
	};

	std::string to_hex(uint8_t* input, size_t len);

	inline std::string to_sha1(const std::string& input)
	{
		sha1 checksum;
		checksum.update(reinterpret_cast<const uint8_t *>(input.c_str()), input.size());

		uint8_t digest[sha1::DIGEST_SIZE];
		checksum.final(digest);

		return to_hex(digest, sha1::DIGEST_SIZE);
	}

	inline std::string to_sha256(std::string input)
	{
		sha256 checksum;
		checksum.update(reinterpret_cast<const uint8_t *>(reinterpret_cast<const uint8_t *>(input.c_str())), input.size());

		uint8_t digest[sha256::DIGEST_SIZE];
		checksum.final(digest);

		return to_hex(digest, sha256::DIGEST_SIZE);
	}
}
