#include "custom_decryptor.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"

void CustomDecryptor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse_and_decrypt", "file", "key", "non_pack_file"), &CustomDecryptor::parse_and_decrypt);
	ClassDB::bind_method(D_METHOD("is_file_nonpck_encrypted", "path"), &CustomDecryptor::is_file_nonpck_encrypted);
	ClassDB::bind_method(D_METHOD("get_required_key_size_in_bytes"), &CustomDecryptor::get_required_key_size_in_bytes);
	GDVIRTUAL_BIND(_parse_and_decrypt, "file", "key", "non_pack_file");
	GDVIRTUAL_BIND(_is_file_nonpck_encrypted, "file");
	GDVIRTUAL_BIND(_get_required_key_size_in_bytes);
}

Dictionary CustomDecryptor::parse_and_decrypt(Ref<FileAccess> p_file, const Vector<uint8_t> &p_key, bool p_non_pack_file) {
	Dictionary result;
	GDVIRTUAL_CALL(_parse_and_decrypt, p_file, p_key, p_non_pack_file, result);
	return result;
}

bool CustomDecryptor::is_file_nonpck_encrypted(Ref<FileAccess> p_file) {
	bool result = false;
	GDVIRTUAL_CALL(_is_file_nonpck_encrypted, p_file, result);
	return result;
}

int CustomDecryptor::get_required_key_size_in_bytes() const {
	int result = 32;
	GDVIRTUAL_CALL(_get_required_key_size_in_bytes, result);
	return result;
}
