#pragma once
#include "Runtime.h"
#include <mbedtls/ecdh.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
namespace Competition {
inline bool pairAckTag(const uint8_t* key,uint64_t controller,uint64_t device,uint64_t session,uint8_t* tag) {
  uint8_t message[32]={},digest[32]={};memcpy(message,"PAIR-ACK",8);
  put(message+8,controller,8);put(message+16,device,8);put(message+24,session,8);
  bool ok=mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),key,16,message,sizeof(message),digest)==0;
  if(ok)memcpy(tag,digest,16);memset(digest,0,sizeof(digest));return ok;
}
inline bool validPairAck(const uint8_t* key,const Packet& packet) {
  uint8_t expected[16];if(!pairAckTag(key,packet.controllerBoot,packet.deviceBoot,packet.match,expected))return false;
  uint8_t diff=0;for(size_t i=0;i<16;++i)diff|=expected[i]^packet.payload[i];return diff==0;
}
// Ephemeral P-256 ECDH: only public keys travel in discovery broadcasts.
// Initial enrollment is first-claim; replacing a binding requires local consent.
class PairingCrypto {
  mbedtls_ecp_group group_;
  mbedtls_ecp_point public_;
  mbedtls_mpi private_;
  bool ready_=false;
  static int random(void*,unsigned char* out,size_t n){esp_fill_random(out,n);return 0;}
public:
  uint8_t bytes[65]={};
  PairingCrypto(){mbedtls_ecp_group_init(&group_);mbedtls_ecp_point_init(&public_);mbedtls_mpi_init(&private_);}
  ~PairingCrypto(){mbedtls_mpi_free(&private_);mbedtls_ecp_point_free(&public_);mbedtls_ecp_group_free(&group_);}
  PairingCrypto(const PairingCrypto&)=delete;
  PairingCrypto& operator=(const PairingCrypto&)=delete;
  bool begin(){
    size_t n=0;
    ready_=mbedtls_ecp_group_load(&group_,MBEDTLS_ECP_DP_SECP256R1)==0 &&
      mbedtls_ecdh_gen_public(&group_,&private_,&public_,random,nullptr)==0 &&
      mbedtls_ecp_point_write_binary(&group_,&public_,MBEDTLS_ECP_PF_UNCOMPRESSED,&n,bytes,sizeof(bytes))==0 && n==65;
    return ready_;
  }
  bool derive(const uint8_t* peer,uint64_t controller,uint64_t device,uint64_t session,uint8_t* key){
    if(!ready_)return false;
    mbedtls_ecp_point q;mbedtls_ecp_point_init(&q);
    mbedtls_mpi secret;mbedtls_mpi_init(&secret);
    uint8_t material[56]={},digest[32]={};
    bool ok=mbedtls_ecp_point_read_binary(&group_,&q,peer,65)==0 &&
      mbedtls_ecp_check_pubkey(&group_,&q)==0 &&
      mbedtls_ecdh_compute_shared(&group_,&secret,&q,&private_,random,nullptr)==0 &&
      mbedtls_mpi_write_binary(&secret,material,32)==0;
    put(material+32,controller,8);put(material+40,device,8);put(material+48,session,8);
    ok=ok && mbedtls_sha256_ret(material,sizeof(material),digest,0)==0;
    if(ok)memcpy(key,digest,16);
    memset(material,0,sizeof(material));memset(digest,0,sizeof(digest));
    mbedtls_mpi_free(&secret);mbedtls_ecp_point_free(&q);return ok;
  }
};
}
