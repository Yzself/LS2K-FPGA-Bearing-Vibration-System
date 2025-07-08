`timescale 1ns / 1ps

//全双工SPI从机驱动
//全局时钟clk至少是sclk可能出现的最高频率的四倍
//sclk频率取决于连接线长度强度，以及是否使用专用时钟接口
//板载连接未使用专用时钟接口的情况下，sclk不建议超过50MHZ(对应从机clk就是200Mhz)
//使用杜邦线连接的情况下，sclk不建议超过25MHZ(对应从机clk就是100Mhz)
//rec_data为主机发过来的数据，在rec_ready为高电平时取出
//send_data为从机响应主机数据，一开始放入第一个响应数据
//上层收到send_ready的高电平可以更新send_data
//在一次传输完全结束（cs_n被拉高后），done会返回一个高电平。
//mode需要和主机mode保持一致
module spi_slave_driver(
    output reg[7:0] rec_data,
    output rec_ready,
    output reg send_ready,
    output reg done,
    output reg miso,
    input mosi,
    input sclk,
    input cs_n,
    input[7:0] send_data,
    input[1:0] mode,
    input clk,
    input rst
    );
    
    localparam IDLE = 0,
               WR_RD = 1;
    
    wire send_buf_reload03;
    wire send_buf_reload12;
    wire[2:0] rec_cnt03;
    wire[2:0] rec_cnt12;
    wire[2:0] send_cnt03;
    wire[2:0] send_cnt12;
    
    reg state;
    reg miso03;
    reg miso12;
    reg cnt_rst;
    reg rec_ready03;
    reg rec_ready12;
    reg rec_ready_sel;
    reg send_reload03;
    reg send_reload12;
    reg send_ready_buf;
    reg[7:0] rec_buf03;
    reg[7:0] rec_buf12;
    reg[7:0] send_buf03;
    reg[7:0] send_buf12;
    reg[7:0] rec_data03;
    reg[7:0] rec_data12;
    
    assign send_buf_reload03 = cnt_rst || send_reload03;
    assign send_buf_reload12 = cnt_rst || send_reload12;
    
    always@(posedge clk or negedge rst)
    begin
        if(!rst) 
            state <= IDLE;
        else begin
            case(state)
                IDLE:
                    if(!cs_n) begin
                        if(mode[0] ^ mode[1]) begin
                            if(sclk)
                                state <= WR_RD;
                        end
                        else begin
                            if(!sclk)
                                state <= WR_RD;
                        end
                    end
                
                WR_RD:
                    if(cs_n)
                        state <= IDLE;
                
                default:
                    state <= IDLE;
            endcase
        end
    end
    
    always@(posedge clk or negedge rst) begin
        if(!rst)
            done <= 0;
        else if(state == WR_RD && cs_n)
            done <= 1;
        else
            done <= 0;
    end
    
    always@(posedge sclk or negedge rst)
    begin
        if(!rst)
            rec_buf03 <= 0;
        else if(state == WR_RD)
            rec_buf03 <= {rec_buf03[6:0],mosi};
    end
    
    always@(negedge sclk or negedge rst)
    begin
        if(!rst)
            rec_buf12 <= 0;
        else if(state == WR_RD)
            rec_buf12 <= {rec_buf12[6:0],mosi};
    end
    
    always@(posedge sclk or negedge rst)
    begin
        if(!rst)
            rec_data03 <= 0;
        else if(rec_cnt03_done)
            rec_data03 <= {rec_buf03[6:0],mosi};
    end
    
    always@(negedge sclk or negedge rst)
    begin
        if(!rst)
            rec_data12 <= 0;
        else if(rec_cnt12_done)
            rec_data12 <= {rec_buf12[6:0],mosi};
    end
    
    always@(posedge sclk or negedge rst)
    begin
        if(!rst)
            rec_ready03 <= 0;
        else if(rec_cnt03_done)
            rec_ready03 <= 1;
        else
            rec_ready03 <= 0;
    end
    
    always@(negedge sclk or negedge rst)
    begin
        if(!rst)
            rec_ready12 <= 0;
        else if(rec_cnt12_done)
            rec_ready12 <= 1;
        else
            rec_ready12 <= 0;
    end
    
    always@(negedge sclk or negedge rst)
    begin
        if(!rst) begin
            send_buf03 <= 0;
            send_buf12 <= 0;
        end
        else begin
            if(send_buf_reload03)
                send_buf03 <= send_data;
            if(send_buf_reload12)
                send_buf12 <= send_data;
        end
    end
    
    always@(posedge sclk or negedge rst) begin
        if(!rst)
            send_reload03 <= 0;
        else if(send_cnt03_done)
            send_reload03 <= 1;
        else
            send_reload03 <= 0;
    end
    
    always@(negedge sclk or negedge rst) begin
        if(!rst)
            send_reload12 <= 0;
        else if(send_cnt12_done)
            send_reload12 <= 1;
        else
            send_reload12 <= 0;
    end
    
    always@(posedge clk or negedge rst)
    begin
        if(!rst)
            rec_data <= 0;
        else begin
            case(mode)
                0: rec_data <= rec_data03;
                1: rec_data <= rec_data12;
                2: rec_data <= rec_data12;
                3: rec_data <= rec_data03;
                default: rec_data <= rec_data03;
            endcase
        end
    end
    
    always@(posedge clk or negedge rst) begin
        if(!rst)
            send_ready <= 0;
        else begin
            case(mode)
                0: send_ready <= send_ready03;
                1: send_ready <= send_ready12;
                2: send_ready <= send_ready12;
                3: send_ready <= send_ready03;
                default: send_ready <= send_ready03;
            endcase
        end
    end
    
    always@(posedge clk or negedge rst) begin
        if(!rst)
            rec_ready_sel <= 0;
        else begin
            case(mode)
                0: rec_ready_sel <= rec_ready03;
                1: rec_ready_sel <= rec_ready12;
                2: rec_ready_sel <= rec_ready12;
                3: rec_ready_sel <= rec_ready03;
                default: rec_ready_sel <= rec_ready03;
            endcase
        end
    end
    
    always@(*) begin
        miso12 = send_buf12[send_cnt12];
    end
    
    always@(posedge clk) begin
        miso03 = send_buf03[send_cnt03];
    end
    
    always@(*) begin
        case(mode)
            0: miso <= miso03;
            1: miso <= miso12;
            2: miso <= miso12;
            3: miso <= miso03;
            default: miso <= miso03;
        endcase
    end
    
    always@(*) begin
        cnt_rst = rst || (state == IDLE);
    end
    
    pulse_conver #(.width(3),.filt_value(3'b100)) PC0(
        .pulse_out(send_ready03),
        .pulse_in(send_buf_reload03),
        .clk(clk),
        .rst(rst)
        );
    
    pulse_conver #(.width(3),.filt_value(3'b100)) PC1(
        .pulse_out(send_ready12),
        .pulse_in(send_buf_reload12),
        .clk(clk),
        .rst(rst)
        );
    
    pulse_conver #(.width(3),.filt_value(3'b001)) PC2(
        .pulse_out(rec_ready),
        .pulse_in(rec_ready_sel),
        .clk(clk),
        .rst(rst)
        );
    
    cnt_negedge #(
        .dir(1),
        .max_value(8)
        ) CNT0
        (
        .cnt_value(send_cnt03),
        .done(send_cnt03_done),
        .clk(sclk),
        .rst(cnt_rst)
        );
    
    cnt #(
        .dir(1),
        .max_value(8)
        ) CNT1
        (
        .cnt_value(rec_cnt03),
        .done(rec_cnt03_done),
        .clk(sclk),
        .rst(cnt_rst)
        );
    
    cnt_negedge #(
        .dir(1),
        .max_value(8)
        ) CNT2
        (
        .cnt_value(send_cnt12),
        .done(send_cnt12_done),
        .clk(sclk),
        .rst(cnt_rst)
        );
    
    cnt_negedge #(
        .dir(1),
        .max_value(8)
        ) CNT3
        (
        .cnt_value(rec_cnt12),
        .done(rec_cnt12_done),
        .clk(sclk),
        .rst(cnt_rst)
        );
    
endmodule
