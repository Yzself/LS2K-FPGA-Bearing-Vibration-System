`timescale 1ns / 1ps

module top(
    // 系统
    input sys_clk/* synthesis PAP_MARK_DEBUG="1" */,
    input rst,
    // AD0 接口
    input  [9:0]  ad0_data/* synthesis PAP_MARK_DEBUG="1" */,
    input         ad0_otr/* synthesis PAP_MARK_DEBUG="1" */,
    output        ad0_clk/* synthesis PAP_MARK_DEBUG="1" */,
    output        ad0_oe/* synthesis PAP_MARK_DEBUG="1" */,
    // AD1 接口
    input  [9:0]  ad1_data/* synthesis PAP_MARK_DEBUG="1" */,
    input         ad1_otr/* synthesis PAP_MARK_DEBUG="1" */,
    output        ad1_clk/* synthesis PAP_MARK_DEBUG="1" */,
    output        ad1_oe/* synthesis PAP_MARK_DEBUG="1" */ ,
    // AD2 接口
    input  [9:0]  ad2_data/* synthesis PAP_MARK_DEBUG="1" */,
    input         ad2_otr/* synthesis PAP_MARK_DEBUG="1" */,
    output        ad2_clk/* synthesis PAP_MARK_DEBUG="1" */,
    output        ad2_oe/* synthesis PAP_MARK_DEBUG="1" */ ,
    // SPI 从机
    input  mosi,
    output miso,
    input  sclk,
    input  cs_n,
    output [3:0]led,
    output test
);

    //=============== 可配置参数 ===============//
    
    // 自定义识别符（2字节）
    parameter DEVICE_IP     = 8'h16;     // 设备识别符 

    parameter DEVICE_Set    = 8'h3f;     // 采样指令符 
    parameter DEVICE_Get    = 8'hf6;     // 读取指令符 
    parameter DEVICE_Div    = 8'h74;     // 读取指令符 
 
    parameter START_ID_H    = 8'hAA;     // 起始校验符
    parameter START_ID_L    = 8'h55;     // 起始校验符
    parameter SEPARATOR1_H  = 8'h5A;     // 分隔校验符1 
    parameter SEPARATOR1_L  = 8'hA5;     // 分隔校验符1 
    parameter SEPARATOR2_H  = 8'h7B;     // 分隔校验符2 
    parameter SEPARATOR2_L  = 8'h89;     // 分隔校验符2 

    parameter END_ID_H      = 8'hFF;     // 结束校验符
    parameter END_ID_L      = 8'hEE;     // 结束校验符
 
    parameter Buffer_Length = 'd1024;     // 采样点个数

    parameter DEFAULT_DIV_CFG = 32'd500;  // 采样频率为10k: clk_10m/10kHz/2=500 
    //=============== 信号声明 ===============//

    // LED
    reg [3:0] led_reg = 4'b0011;  

    // ADC

    reg [9:0] ad0_buffer [0:Buffer_Length-1];
    reg [9:0] ad1_buffer [0:Buffer_Length-1];
    reg [9:0] ad2_buffer [0:Buffer_Length-1];

    reg [10:0] adc_wr_addr;  // 独立地址指针
    reg adc_lock;
    reg adc_full;
    reg [12:0] data_index;  // 支持最大8092字节
    
    reg [31:0] div_cfg = DEFAULT_DIV_CFG; 
    reg [7:0] div_cfg_buffer [0:3]; // 4字节接收缓冲区
    reg [1:0] div_bytes_received;   // 已接收字节计数器

    // SPI 状态机
    reg[2:0] fpga_state;
    localparam state_idle = 0,
               state_ready = 1,
               state_send = 2,
               state_set = 3,
               state_div = 4;
    // SPI
    wire clk_100m;
    wire [7:0] spi_recv_data;
    reg  [7:0] spi_send_data;
    wire spi_recv_ready;
    wire spi_send_ready;
    
    assign led = led_reg;
    assign ad0_oe = 1'b0;
    assign ad1_oe = 1'b0;
    assign ad2_oe = 1'b0;
    assign ad0_clk = adc_clk;
    assign ad1_clk = adc_clk;
    assign ad2_clk = adc_clk;

    assign adc_clk = clk_div_out;   // ADC 采样时钟
     //assign test = clk_44k1;   // ADC 采样时钟

    //=============== SPI从机驱动实例化 ===============//
    spi_slave_driver SPI_SLAVE(
        .rec_data(spi_recv_data),
        .rec_ready(spi_recv_ready),
        .send_ready(spi_send_ready),
        .done(),
        .miso(miso),
        .mosi(mosi),
        .sclk(sclk),
        .cs_n(cs_n),
        .send_data(spi_send_data),
        .mode(2'b00),
        .clk(clk_100m),
        .rst(rst)
    );

    //=============== ADC数据采集 ===============//
    
    always @(posedge adc_clk or negedge rst) begin
        if (!rst) begin
            adc_wr_addr <= 0;
            adc_full <= 0;
        end else if(!adc_lock||!adc_full) begin
            ad0_buffer[adc_wr_addr] <= ad0_data;
            ad1_buffer[adc_wr_addr] <= ad1_data;
            ad2_buffer[adc_wr_addr] <= ad2_data;
            if(adc_wr_addr == Buffer_Length-1)begin
                adc_wr_addr <= 0;
                adc_full <= 1;
            end else begin
                adc_wr_addr <= adc_wr_addr + 1;
                adc_full <= 0;
            end
        end
    end

    //=============== 控制状态机 ===============//
    always @(negedge clk_100m or negedge rst) begin
        if (!rst) begin
            fpga_state <= state_idle;
            led_reg <= 4'b0011;
            adc_lock <= 0;
            div_bytes_received <= 0;
            div_cfg <= DEFAULT_DIV_CFG;  // 复位时加载默认值
        end else if(!cs_n) begin
            if (spi_recv_ready) begin  //  保证数据是刚刚收到
                case (fpga_state)
                    state_idle:begin
                        case (spi_recv_data)
                            DEVICE_IP: begin
                                fpga_state <= state_ready;
                            end
                            default: fpga_state <= state_idle;
                        endcase
                    end
                    state_ready:begin
                        case (spi_recv_data)
                            DEVICE_Div: begin
                                fpga_state <= state_div;
                            end
                            DEVICE_Set: begin
                                fpga_state <= state_set;
                            end
                            DEVICE_Get: begin
                                fpga_state <= state_send;
                                led_reg <= led_reg ^ 4'b1111; 
                            end
                            default:  fpga_state <= state_idle;
                        endcase
                    end
                    state_send: fpga_state <= state_send;
                    state_set: fpga_state <= state_set;
                    state_div:begin
                    // 接收4字节分频系数
                    if (div_bytes_received < 3) begin
                        div_cfg_buffer[div_bytes_received] <= spi_recv_data;
                        div_bytes_received <= div_bytes_received + 1;
                    end 
                    else begin
                        // 接收第4字节后更新分频系数
                        div_cfg <= {spi_recv_data, 
                                   div_cfg_buffer[2],
                                   div_cfg_buffer[1],
                                   div_cfg_buffer[0]};  // 小端模式转大端
                        div_bytes_received <= 0;
                        fpga_state <= state_set;  // 返回空闲状态
                    end
                end
                    
                endcase
            end
        end else begin
            div_bytes_received <= 0;  // CS_N变高时重置接收计数器
            if(fpga_state == state_set) adc_lock <= 1;
            else if(fpga_state == state_send) adc_lock <= 0;
            fpga_state <= state_idle;
            div_cfg <= div_cfg;
        end
    end

    //=============== 数据发送逻辑 ===============//
    always @(posedge clk_100m or negedge rst) begin
        if (!rst) begin
            data_index <= 0;
            spi_send_data <= 8'h00;
        end else begin
            if (!cs_n) begin
                if (spi_recv_ready) begin
                    if(fpga_state == state_send) begin
                        case(data_index)
                            // 起始标识
                            0: spi_send_data <= START_ID_H;
                            1: spi_send_data <= START_ID_L;
                            
                            // AD1数据区（512点×2字节）
                            default: begin
                                if(data_index < Buffer_Length*2+2) begin // Buffer_Length*2+2
                                    if(data_index[0])  // 奇数字节发低位
                                        spi_send_data <= {6'b0, ad0_buffer[(data_index-2)>>1][1:0]};
                                    else               // 偶数字节发高位
                                        spi_send_data <= ad0_buffer[(data_index-2)>>1][9:2];
                                end
                                else if(data_index < Buffer_Length*2+4) begin // 分隔符1 Buffer_Length*2+4
                                    spi_send_data <= (data_index == Buffer_Length*2+2) ? SEPARATOR1_H : SEPARATOR1_L;
                                end
                                else if(data_index < (Buffer_Length*4+4)) begin // AD0数据区 Buffer_Length*4+4
                                    if(data_index[0])
                                        spi_send_data <= {6'b0, ad1_buffer[(data_index-(Buffer_Length*2+4))>>1][1:0]};
                                    else
                                        spi_send_data <= ad1_buffer[(data_index-(Buffer_Length*2+4))>>1][9:2];
                                end
                                else if(data_index < (Buffer_Length*4+6)) begin // 分隔符2 Buffer_Length*4+6
                                    spi_send_data <= (data_index == (Buffer_Length*4+4)) ? SEPARATOR2_H : SEPARATOR2_L;
                                end
                                else if(data_index < (Buffer_Length*6+6)) begin // AD0数据区 Buffer_Length*6+6
                                    if(data_index[0])
                                        spi_send_data <= {6'b0, ad2_buffer[(data_index-(Buffer_Length*4+6))>>1][1:0]};
                                    else
                                        spi_send_data <= ad2_buffer[(data_index-(Buffer_Length*4+6))>>1][9:2];
                                end
                                else if(data_index < (Buffer_Length*6+8)) begin // 结束符 Buffer_Length*6+8
                                    spi_send_data <= (data_index == (Buffer_Length*6+6)) ? END_ID_H : END_ID_L;
                                end
                                else begin
                                    spi_send_data <= 8'h00;
                                end
                            end
                        endcase
                        data_index <= (data_index == (Buffer_Length*6+8)) ? 0 : data_index + 1;
                    end else if(fpga_state == state_set)begin
                        case(data_index)
                            // 起始标识
                            0: spi_send_data <= END_ID_H;
                            1: spi_send_data <= END_ID_L;
                            default: spi_send_data <= 8'h00;
                        endcase
                        data_index <= (data_index == 1) ? 0 : data_index + 1;
                    end
                end
            end else begin
                data_index <= 0;
                spi_send_data <= 8'h00;
            end
        end
    end

    
    pllclk pll_clk (
    .pll_rst(~rst),      // input
    .clkin1(sys_clk),        // input
    .clkout0(clk_100m),      // output
    .clkout1(clk_25m),      // output
    .clkout2(clk_10m),      // output
    .clkout3(clk_5m),      // output
    .clkout4(clk_2m)       // output
    );

   adc_clk_divider u_divider (
    .clk_10m(clk_10m),
    .rst_n(rst),
    .div_cfg(div_cfg),  //  采样频率为10k: clk_10m/10kHz/2=500 //采样频率为44.1k:clk_10m/44.1kHz/2=113 
    .clk_out(clk_div_out)
);

endmodule