-- derived from q67, udf in projection, no improv using cache;
select
from (select i_category
            ,i_class
            ,i_brand
            ,i_product_name
            ,d_year
            ,d_qoy
            ,d_moy
            ,s_store_id, i_des
            ,sumsales
            ,rank() over (partition by i_category order by sumsales desc) rk
      from (select i_category
                  ,i_class
                  ,i_brand
                  ,i_product_name
                  ,d_year
                  ,d_qoy
                  ,d_moy
                  ,s_store_id, sudf_string_manipulation(i_item_desc, ',',',') i_des,
                  sum(coalesce(ss_sales_price*ss_quantity,0)) sumsales
            from store_sales
                ,date_dim
                ,store
                ,item
       where  ss_sold_date_sk=d_date_sk
          and ss_item_sk=i_item_sk
          and ss_store_sk = s_store_sk
          and d_month_seq between 1181 and 1181+11 
       group by  rollup(i_category, i_class, i_brand, i_product_name, d_year, d_qoy, d_moy,s_store_id, i_des))dw1) dw2
where rk <= 100
order by i_category
        ,i_class
        ,i_brand
        ,i_product_name
        ,d_year
        ,d_qoy
        ,d_moy
        ,s_store_id
        ,sumsales
        ,rk
limit 100;


-- derived from q65, udf in projection, no improv using cache;
SELECT s_store_name, 
sudf_string_manipulation(i_item_desc, ',',',') i_des,               sc.revenue, 
               i_current_price, 
               i_wholesale_cost, 
               i_brand 
FROM   store, 
       item, 
       (SELECT ss_store_sk, 
               Avg(revenue) AS ave 
        FROM   (SELECT ss_store_sk, 
                       ss_item_sk, 
                       Sum(ss_sales_price) AS revenue 
                FROM   store_sales, 
                       date_dim 
                WHERE  ss_sold_date_sk = d_date_sk 
                       AND d_month_seq BETWEEN 1199 AND 1199 + 11 
                GROUP  BY ss_store_sk, 
                          ss_item_sk) sa 
        GROUP  BY ss_store_sk) sb, 
       (SELECT ss_store_sk, 
               ss_item_sk, 
               Sum(ss_sales_price) AS revenue 
        FROM   store_sales, 
               date_dim 
        WHERE  ss_sold_date_sk = d_date_sk 
               AND d_month_seq BETWEEN 1199 AND 1199 + 11 
        GROUP  BY ss_store_sk, 
                  ss_item_sk) sc 
WHERE  sb.ss_store_sk = sc.ss_store_sk 
       AND sc.revenue <= 0.1 * sb.ave 
       AND s_store_sk = sc.ss_store_sk 
       AND i_item_sk = sc.ss_item_sk 
ORDER  BY s_store_name, 
          i_item_desc
LIMIT 100; 


-- derived from q67, udf in projection and predicate, no improv using cache;
select
from (select i_category
            ,i_class
            ,i_brand
            ,i_product_name
            ,d_year
            ,d_qoy
            ,d_moy
            ,s_store_id, i_des
            ,sumsales
            ,rank() over (partition by i_category order by sumsales desc) rk
      from (select i_category
                  ,i_class
                  ,i_brand
                  ,i_product_name
                  ,d_year
                  ,d_qoy
                  ,d_moy
                  ,s_store_id, sudf_string_manipulation(i_item_desc, ',',',') i_des,
                  sum(coalesce(ss_sales_price*ss_quantity,0)) sumsales
            from store_sales
                ,date_dim
                ,store
                ,item
       where  ss_sold_date_sk=d_date_sk
          and ss_item_sk=i_item_sk
          and ss_store_sk = s_store_sk
          and d_month_seq between 1181 and 1181+11 and sudf_string_manipulation(i_item_desc, ',',',') is not null and sudf_string_manipulation(i_item_desc, ',',',') != ''
       group by  rollup(i_category, i_class, i_brand, i_product_name, d_year, d_qoy, d_moy,s_store_id, i_des))dw1) dw2
where rk <= 100
order by i_category
        ,i_class
        ,i_brand
        ,i_product_name
        ,d_year
        ,d_qoy
        ,d_moy
        ,s_store_id
        ,sumsales
        ,rk
limit 100;

-- derived from q65, udf in projection and predicate, no improv using cache;
SELECT s_store_name, 
sudf_string_manipulation(i_item_desc, ',',',') i_des,               sc.revenue, 
               i_current_price, 
               i_wholesale_cost, 
               i_brand 
FROM   store, 
       item, 
       (SELECT ss_store_sk, 
               Avg(revenue) AS ave 
        FROM   (SELECT ss_store_sk, 
                       ss_item_sk, 
                       Sum(ss_sales_price) AS revenue 
                FROM   store_sales, 
                       date_dim 
                WHERE  ss_sold_date_sk = d_date_sk 
                       AND d_month_seq BETWEEN 1199 AND 1199 + 11 
                GROUP  BY ss_store_sk, 
                          ss_item_sk) sa 
        GROUP  BY ss_store_sk) sb, 
       (SELECT ss_store_sk, 
               ss_item_sk, 
               Sum(ss_sales_price) AS revenue 
        FROM   store_sales, 
               date_dim 
        WHERE  ss_sold_date_sk = d_date_sk 
               AND d_month_seq BETWEEN 1199 AND 1199 + 11 
        GROUP  BY ss_store_sk, 
                  ss_item_sk) sc 
WHERE  sb.ss_store_sk = sc.ss_store_sk 
       AND sc.revenue <= 0.1 * sb.ave 
       AND s_store_sk = sc.ss_store_sk 
       AND i_item_sk = sc.ss_item_sk and sudf_string_manipulation(i_item_desc, ',',',') is not null AND sudf_string_manipulation(i_item_desc, ',',',') != ''
ORDER  BY s_store_name, 
          i_item_desc
LIMIT 100; 

-- derived from q92, udf in predicate, no improv using cache;
SELECT 
         Sum(ws_ext_discount_amt)
FROM     web_sales , 
         item , 
         date_dim 
WHERE    i_manufact_id = 718 AND sudf_string_manipulation(i_item_desc, ',',',') is not null and sudf_string_manipulation(i_item_desc, ',',',') != ''
AND      i_item_sk = ws_item_sk 
AND      d_date BETWEEN '2002-03-29' AND      ( 
                  Cast('2002-03-29' AS DATE) +  INTERVAL '90' day) 
AND      d_date_sk = ws_sold_date_sk 
AND      ws_ext_discount_amt > 
         ( 
                SELECT 1.3 * avg(ws_ext_discount_amt) 
                FROM   web_sales , 
                       date_dim 
                WHERE  ws_item_sk = i_item_sk 
                AND    d_date BETWEEN '2002-03-29' AND    ( 
                              cast('2002-03-29' AS date) + INTERVAL '90' day) 
                AND    d_date_sk = ws_sold_date_sk ) 
ORDER BY sum(ws_ext_discount_amt) 
LIMIT 100; 