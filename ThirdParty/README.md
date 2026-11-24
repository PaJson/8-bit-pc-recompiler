# ThirdParty

Files by other people that this project passes on **unchanged**, because their terms allow it.
They are not our work and not under our licence; each keeps its own terms. Tools/make_release.py
copies them into a release (`ROMs/<platform>/Patches/...`), where the launcher finds them by CRC32.

## sms/Patches/Phantasy Star/Retranslation 2.6.1 (PSRP, SMS Power)/

The seven official release zips of the Phantasy Star Retranslation 2.6.1 (English + literal, French +
extended, German, Spanish, Catalan, Latin, Brazilian Portuguese), byte-identical to the downloads:

- Authors: Maxim, Paul Jensen, Z80 Gaiden and the SMS Power! team, with the translators named in each
  zip's README.md.
- Official pages: https://www.smspower.org/Translations/PhantasyStar-SMS-EN (and `-FR`, `-DE`, `-ES`,
  `-CA`, `-PT-BR`); source: https://github.com/maxim-zhao/psrp
- Terms (README.md, "Disclaimer", in every zip): "Any enclosed files are offered at no charge and must
  be distributed together in original condition. These files _may not_ be released for commercial sales
  without explicit authorisation from all parties who own copyright ownership."

So: keep each zip exactly as it is (never unpack, edit or re-pack it here), never charge for it.
Each zip patches the original **Japanese** Phantasy Star ROM, which the player supplies.

SHA-256 of the files as downloaded:

```
1aff11dc86cf811695a90855e1fa3f8a5fc4baf7f2ab817b38bbb7ae3bed6867  PhantasyStar-SMS-EN-2.6.1.zip
71e4c7728332356e03ba6b2ec9c8acc3e0d8a78f16a74b10c32cf4e0071018e5  PhantasyStar-SMS-FR-2.6.1.zip
782fde58df1dd832971a489da72f5a1977d9b85bf723cd119e085ddeda5f4493  PhantasyStar-SMS-ca-2.6.1.zip
8c24ce38a847eb52800669ad9218291b5253f4b9a7cefa191449ad667468a54e  PhantasyStar-SMS-de-2.6.1.zip
5da2dfe324a94cadfe01a1241d67adf7f4632210aab9e00f9140df5d2653ac6f  PhantasyStar-SMS-es-2.6.1.zip
5d6ee9c2723258ac5301c58001ea95a473c1d5fed540e45fd23d629f7873b2e2  PhantasyStar-SMS-la-2.6.1.zip
7da62ce723b9f519594eb263a949055cf4e81d6048330edf3546d6b9950b7436  PhantasyStar-SMS-pt-br-2.6.1.zip
```
