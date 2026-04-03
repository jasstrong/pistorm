/*
 * PiStorm Mac68k CPLD — v4 (full 8-state bus cycle)
 * Based on PiStormX 8-state design by FLACO (CC-BY-NC-SA)
 * Adapted for original PiStorm EPM240 with external latches.
 *
 * Full 8-state cycle (S0-S7) matches real 68000 timing.
 * Write data held through S5/S6/S7 as the BBU expects.
 * BERR handling + STATUS_BIT_INIT state machine reset.
 */
module pistorm(
    output          PI_TXN_IN_PROGRESS, // GPIO0
    output          PI_IPL_ZERO,        // GPIO1
    input   [1:0]   PI_A,       // GPIO[3..2]
    input           PI_CLK,     // GPIO4 — not used (kept for pin compat)
    output          PI_RESET,   // GPIO5
    input           PI_RD,      // GPIO6
    input           PI_WR,      // GPIO7
    inout   [15:0]  PI_D,       // GPIO[23..8]

    output          LTCH_A_0,
    output          LTCH_A_8,
    output          LTCH_A_16,
    output          LTCH_A_24,
    output          LTCH_A_OE_n,
    output          LTCH_D_RD_U,
    output          LTCH_D_RD_L,
    output          LTCH_D_RD_OE_n,
    output          LTCH_D_WR_U,
    output          LTCH_D_WR_L,
    output          LTCH_D_WR_OE_n,

    input           M68K_CLK,
    output  [2:0]   M68K_FC,

    output          M68K_AS_n,
    output          M68K_UDS_n,
    output          M68K_LDS_n,
    output          M68K_RW,

    input           M68K_DTACK_n,
    input           M68K_BERR_n,

    input           M68K_VPA_n,
    output          M68K_E,
    output          M68K_VMA_n,

    input   [2:0]   M68K_IPL_n,

    inout           M68K_RESET_n,
    inout           M68K_HALT_n,

    input           M68K_BR_n,
    output          M68K_BG_n,
    input           M68K_BGACK_n
  );

  wire c7m = M68K_CLK;

  localparam REG_DATA = 2'd0;
  localparam REG_ADDR_LO = 2'd1;
  localparam REG_ADDR_HI = 2'd2;
  localparam REG_STATUS = 2'd3;

  assign M68K_FC = 3'd0;
  assign M68K_BG_n = 1'b1;

  // Full 8-state bus cycle
  reg s0 = 1'd1;
  reg s1 = 1'd0;
  reg s2 = 1'd0;
  reg s3 = 1'd0;
  reg s4 = 1'd0;
  reg s5 = 1'd0;
  reg s6 = 1'd0;
  reg s7 = 1'd0;

  reg op_req = 1'b0;
  reg op_rw = 1'b1;
  reg op_a0 = 1'b0;
  reg op_sz = 1'b0;

  reg [15:0] status;
  wire st_reset_out = !status[1];
  reg st_init;
  always @(posedge c7m) begin
    st_init <= status[0];
  end

  reg M68K_VMA_nr = 1'd1;
  reg [3:0] e_counter = 4'd0;
  reg [2:0] ipl;
  reg [2:0] ipl_a;


  // ============================================================
  // RESET
  // ============================================================
  reg [1:0] resetfilter = 2'b11;
  wire oor = resetfilter == 2'b01;
  always @(negedge c7m) begin
    resetfilter <= {resetfilter[0], M68K_RESET_n};
  end
  assign PI_RESET = st_reset_out ? 1'b1 : M68K_RESET_n;
  assign M68K_RESET_n = st_reset_out ? 1'b0 : 1'bz;
  assign M68K_HALT_n = st_reset_out ? 1'b0 : 1'bz;


  // ============================================================
  // E CLOCK
  // ============================================================
  always @(negedge c7m) begin
    if (e_counter == 4'd9)
      e_counter <= 4'd0;
    else
      e_counter <= e_counter + 4'd1;
  end
  assign M68K_E = (e_counter > 4'd5) ? 1'b1 : 1'b0;


  // ============================================================
  // INTERRUPT CONTROL
  // ============================================================
  always @(negedge c7m) begin
    ipl_a <= ~M68K_IPL_n;
    if (ipl_a == ~M68K_IPL_n)
      ipl <= ~M68K_IPL_n;
  end
  assign PI_IPL_ZERO = ipl == 3'd0;


  // ============================================================
  // PI SIDE
  // ============================================================

  assign PI_D = (PI_A == REG_STATUS && PI_RD) ? {ipl, 13'd0} : 16'bz;

  always @(posedge PI_WR) begin
    case (PI_A)
      REG_ADDR_LO: op_a0 <= PI_D[0];
      REG_ADDR_HI: begin
        op_sz <= PI_D[8];
        op_rw <= PI_D[9];
      end
      REG_STATUS: status <= PI_D;
    endcase
  end

  // External latch enables (combinational)
  assign LTCH_D_WR_U = PI_A == REG_DATA && PI_WR;
  assign LTCH_D_WR_L = PI_A == REG_DATA && PI_WR;
  assign LTCH_A_0  = PI_A == REG_ADDR_LO && PI_WR;
  assign LTCH_A_8  = PI_A == REG_ADDR_LO && PI_WR;
  assign LTCH_A_16 = PI_A == REG_ADDR_HI && PI_WR;
  assign LTCH_A_24 = PI_A == REG_ADDR_HI && PI_WR;

  assign LTCH_D_RD_OE_n = !(PI_A == REG_DATA && PI_RD);


  // ============================================================
  // PI ↔ 68K synchronization
  // ============================================================

  assign PI_TXN_IN_PROGRESS = op_req;

  // Release at S4 (after DTACK received, data captured for reads)
  wire op_reqrst = s4 | oor | st_init;
  wire op_reqset = PI_WR & (PI_A == REG_ADDR_HI);
  always @(posedge op_reqset, posedge op_reqrst) begin
    if (op_reqrst)
      op_req <= 1'b0;
    else
      op_req <= 1'b1;
  end


  // ============================================================
  // 68K BUS STATE MACHINE (full 8-state: S0-S7)
  // ============================================================

  wire s1rst = s2 | oor | st_init;
  wire s2rst = s3 | oor | st_init;
  wire s3rst = s4 | oor | st_init;
  wire s4rst = s5 | oor | st_init;
  wire s5rst = s6 | oor | st_init;
  wire s6rst = s7 | oor | st_init;
  wire s7rst = s0 | oor;

  // S0 → S1
  always @(negedge c7m, posedge s1rst) begin
    if (s1rst) s1 <= 1'd0;
    else if (s0) s1 <= 1'd1;
  end

  // S1 → S2 (if op_req)
  always @(posedge c7m, posedge s2rst) begin
    if (s2rst) s2 <= 1'd0;
    else if (s1 && op_req) s2 <= 1'd1;
  end

  // S2 → S3
  always @(negedge c7m, posedge s3rst) begin
    if (s3rst) s3 <= 1'd0;
    else if (s2) s3 <= 1'd1;
  end

  // S3 → S4 (if DTACK or BERR or VMA+E)
  always @(posedge c7m, posedge s4rst) begin
    if (s4rst) s4 <= 1'd0;
    else if (s3 && (!M68K_DTACK_n || !M68K_BERR_n ||
                    (!M68K_VMA_nr && e_counter == 4'd8)))
      s4 <= 1'd1;
  end

  // S4 → S5
  always @(negedge c7m, posedge s5rst) begin
    if (s5rst) s5 <= 1'd0;
    else if (s4) s5 <= 1'd1;
  end

  // S5 → S6
  always @(posedge c7m, posedge s6rst) begin
    if (s6rst) s6 <= 1'd0;
    else if (s5) s6 <= 1'd1;
  end

  // S6 → S7
  always @(negedge c7m, posedge s7rst) begin
    if (s7rst) s7 <= 1'd0;
    else if (s6) s7 <= 1'd1;
  end

  // S7 → S0
  always @(posedge c7m, posedge s1) begin
    if (s1) s0 <= 1'd0;
    else if (s7 | oor | st_init) s0 <= 1'd1;
  end


  // ============================================================
  // 68K BUS SIGNALS — external latch control
  // ============================================================

  // Address latch OE: driven during bus cycle
  assign LTCH_A_OE_n = s0 | (s1 & !op_req);

  // Data write latch OE: driven during write S3-S7 (full hold through S5/S6/S7)
  assign LTCH_D_WR_OE_n = !(!op_rw && (s3 | s4 | s5 | s6 | s7));

  // Data read latch: transparent during S3/S4 for reads
  assign LTCH_D_RD_U = op_rw & (s3 | s4);
  assign LTCH_D_RD_L = op_rw & (s3 | s4);

  // /AS: asserted S2-S6, deasserted S0/S1/S7.
  // Two LCELL delays (~10ns) on deassertion to match the real
  // 68000's edge rate for the BBU.
  wire as_raw_n = s0 | s1 | s7;
  (* keep *) wire as_d1, as_d2;
  LCELL as_delay1 (.in(as_raw_n), .out(as_d1)) /* synthesis keep */;
  LCELL as_delay2 (.in(as_d1), .out(as_d2)) /* synthesis keep */;
  assign M68K_AS_n = as_raw_n & as_d2;

  // /UDS, /LDS
  wire op_ds_n = s0 | s1 | (s2 & !op_rw) | s7;
  assign M68K_UDS_n = op_ds_n | (op_sz & op_a0);
  assign M68K_LDS_n = op_ds_n | (op_sz & !op_a0);

  // R/W
  assign M68K_RW = s0 | s1 | op_rw;

  // VMA
  wire vmarst = s7 | oor | st_init;
  always @(posedge c7m, posedge vmarst) begin
    if (vmarst) M68K_VMA_nr <= 1'b1;
    else if (s3 && !M68K_VPA_n && e_counter == 4'd2)
      M68K_VMA_nr <= 1'b0;
  end
  assign M68K_VMA_n = M68K_VMA_nr;

endmodule
