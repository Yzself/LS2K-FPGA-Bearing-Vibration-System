module adc_clk_divider(
    input        clk_10m,     // 50MHz输入时钟
    input        rst_n,       // 异步复位（低有效）
    input [31:0] div_cfg,     // 分频系数配置（整数部分）
    output reg   clk_out      // 分频输出时钟
);

// 参数定义
parameter DIV_FRAC = 1;       // 固定小数分频系数

reg [31:0] cnt;               // 主计数器
reg [31:0] frac_cnt;          // 小数计数器

// 动态分频系数计算
wire [31:0] current_div = div_cfg + DIV_FRAC;

// 主计数器逻辑
always @(posedge clk_10m or negedge rst_n) begin
    if(!rst_n) begin
        cnt <= 0;
        frac_cnt <= 0;
        clk_out <= 0;
    end
    else begin
        if(cnt == current_div - 1) begin
            cnt <= 0;
            
            // 小数分频控制
            if(frac_cnt == (DIV_FRAC - 1)) begin
                frac_cnt <= 0;
                clk_out <= ~clk_out;
            end
            else begin
                frac_cnt <= frac_cnt + 1;
                // 保持时钟稳定（仅在小数周期翻转）
            end
        end
        else begin
            cnt <= cnt + 1;
        end
    end
end

endmodule