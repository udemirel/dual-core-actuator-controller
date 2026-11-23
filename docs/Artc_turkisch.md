# Donanım ve Çok Çekirdekli Mimari Derinlemesine İnceleme (RP2350)

## 1. Asimetrik Açılış Sırası ve Donanımsal El Sıkışma (`multicore_launch_core1`)

Güç verildiğinde (Power-On Reset), RP2350 içindeki donanımsal BootROM yalnızca **Core 0**'ı başlatır ve uyandırır. **Core 1** ise BootROM içerisinde bir `WFE` (Wait For Event - Olay Bekle) komutu çalıştırarak düşük güç uyku modunda bekler; Core 0 tarafından açıkça serbest bırakılana kadar sıfır CPU döngüsü tüketir ve hiçbir kullanıcı kodu çalıştırmaz.

Bu yapı deterministik bir **Master-Slave Boot** sırası sağlar: Core 0, sistem saatlerini (clock), bellek bölümlerini ve ortak çevre birimlerini yapılandırdıktan sonra **Inter-Core FIFO** üzerinden 5 adımlı bir donanım el sıkışması (handshake) ile Core 1'i uyandırır:

1. **Temizleme ve Uyandırma (`SEV`):** Core 0, FIFO kuyruğunu temizler, içine `0` yazar ve Core 1'i `WFE` uyku durumundan uyandırmak için donanımsal bir `SEV` (Send Event) sinyali gönderir.
2. **El Sıkışma Onayı:** Core 1 uyanır ve başlangıç parametrelerini almaya hazır olduğunu onaylamak için `0` değerini aynen geri gönderir.
3. **Vektör Tablosu (`VTOR`) Yapılandırması:** Core 0, kesme vektör tablosunun (Interrupt Vector Table) bellek adresini iletir; Core 1 bunu kendi `VTOR` register'ına yazar ve onay gönderir.
4. **Stack Pointer (`MSP`) İzolasyonu:** Core 0, Core 1'e ayrılmış özel stack alanının tepe adresini (`SCRATCH_Y` içindeki `__StackOneTop`) gönderir; Core 1 kendi Main Stack Pointer (`SP`) register'ını yükler ve onay gönderir.
5. **Başlangıç Noktası (`PC`) Yürütmesi:** Core 0, hedef fonksiyon işaretçisini (`core1_realtime_entry`) iletir; Core 1 onayı geri gönderir ve Program Counter (`PC`) register'ını bu adrese dallandırarak gerçek zamanlı yürütmeye başlar.

---

## 2. Çekirdekler Arası Donanım FIFO'su (SIO Mailbox Mimarisi)

Inter-Core FIFO, bir Flash depolama alanı veya genel amaçlı SRAM **değildir**; doğrudan her iki çekirdeğin hemen bitişiğindeki **SIO (Single-cycle I/O)** bloğuna entegre edilmiş, silikon seviyesinde özel bir donanım çevre birimidir:

* **Çift Tek Yönlü Boru Hattı:** Birbirinden bağımsız, 32-bit genişliğinde ve 4 giriş derinliğinde iki adet donanım register kuyruğundan (`Core 0 -> Core 1` ve `Core 1 -> Core 0`) oluşur.
* **Kilitsiz (Lock-Free) Atomik Erişim:** Yazma portu (`FIFO_WR`) fiziksel olarak yalnızca gönderen çekirdeğe, okuma portu (`FIFO_RD`) ise yalnızca alan çekirdeğe bağlı olduğu için; 32-bitlik kelimeler veri yolu çakışması veya spinlock gerektirmeden tek bir saat döngüsünde (clock cycle) aktarılır.
* **Donanım Kapı Zili (`SIO_IRQ_FIFO`):** FIFO kuyruğuna bir veri atılması, alıcı çekirdekte otomatik olarak bir donanım kesmesi (`SIO_IRQ_FIFO`) veya olay sinyali tetikler; böylece işlemciyi yoran sürekli RAM yoklama (polling) döngülerini ortadan kaldırır.
* **Hibrit Veri Aktarım Deseni:** Donanım kuyruğunun derinliği `4 x 32-bit` kelime ile sınırlı olduğundan, büyük telemetri verileri Spinlock korumalı **Paylaşımlı SRAM'e (Shared SRAM)** yazılır; **FIFO** ise yalnızca verinin hazır olduğunu bildirmek veya tampon indekslerini iletmek için düşük gecikmeli bir kapı zili olarak kullanılır.

---

## 3. Donanımsal Spinlock'lar ve Kesme Kapatma Farkı

Tek çekirdekli bir mikrodenetleyicide kesmeleri kapatmak (`__disable_irq()`), kritik bir bölgeyi korumak için yeterlidir. Ancak çok çekirdekli bir işlemcide Core 0 üzerindeki kesmeleri kapatmak, **Core 1'in aynı anda aynı SRAM adresine erişmesini durdurmaz**:

* **Atomik Donanım Kilidi:** `spin_lock_blocking()`, bir SIO spinlock register'ı üzerinde tek döngülük atomik "test-et ve kilitle" (test-and-set) işlemi gerçekleştirir **ve** bir kesme hizmet rutininden (ISR) kaynaklanabilecek kendi kendini kilitleme (self-deadlock) durumunu önlemek için çağıran çekirdekteki yerel kesmeleri kapatır.
* **`volatile` Anahtar Kelimesinin Sınırları:** Tek başına `volatile` kullanmak derleyicinin bellek görünürlüğünü ve sıralamasını zorunlu kılar, ancak eşzamanlı çalışan çekirdekler arasında çok alanlı veri yapılarını (struct) güncellerken oluşan veri bölünmesini (*data tearing*) engellemez.

---

## 4. Mekansal Bellek ve Stack İzolasyonu

Stack çakışmalarını önlemek ve havacılık standartlarındaki **Girişimden Bağımsızlık (Freedom From Interference - FFI)** ilkelerini (bkz. CAST-32A / AMC 20-193) karşılamak için:

* **Core 0 Stack Alanı:** `SCRATCH_X` bölümüne atanmıştır (`4 KB` özel SRAM bankası, `0x20080xxx`).
* **Core 1 Stack Alanı:** `SCRATCH_Y` bölümüne atanmıştır (`4 KB` özel SRAM bankası, `0x20081xxx`).
* **Statik Bellek Tahsisi:** Firmware genelinde hiçbir dinamik heap tahsisine (`malloc`/`free`) izin verilmez.