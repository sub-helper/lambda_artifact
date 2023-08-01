package org.example.pgsql_udf;

import static org.postgresql.pljava.annotation.Function.Effects;
import static org.postgresql.pljava.annotation.Function.OnNullInput;
import static org.postgresql.pljava.annotation.Function.Parallel;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.PrintStream;
import java.lang.annotation.RetentionPolicy;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Random;
import java.util.Set;
import org.postgresql.pljava.annotation.Function;
import org.postgresql.pljava.annotation.Function.Effects;

public class sudf_12 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static HashMap<Integer, Double> cache = new HashMap<Integer, Double>();

  public static Double evaluation(Integer cust_sk) throws SQLException {
    Connection conn = DriverManager.getConnection(m_url);
    String query1 =
      "select sum(ws_net_paid_inc_ship_tax) as sum_result from web_sales_history, date_dim where d_date_sk = ws_sold_date_sk and d_year = 2001 and ws_bill_customer_sk=?";
    String query2 =
      "select sum(ws_net_paid_inc_ship_tax) as sum from web_sales_history, date_dim where d_date_sk = ws_sold_date_sk and d_year = 2000 and ws_bill_customer_sk=?";
    PreparedStatement stmt1 = conn.prepareStatement(query1);
    PreparedStatement stmt2 = conn.prepareStatement(query2);
    stmt1.setInt(1, cust_sk);
    stmt2.setInt(1, cust_sk);
    ResultSet rs1 = stmt1.executeQuery();
    ResultSet rs2 = stmt2.executeQuery();
    Double result1 = 0.0;
    Double result2 = 0.0;
    rs1.next();
    rs2.next();
    result1 = rs1.getDouble(1);
    result2 = rs2.getDouble(1);
    if (result1 < result2) {
      return -1.0;
    } else {
      return result1 - result2;
    }
  }

  @Function
  public static Double sudf_12_increaseInWebSpending(Integer cust_sk)
    throws SQLException {
      if (cust_sk == null) {
        return null;
    }
    return evaluation(cust_sk);
  }

  @Function
  public static Double sudf_12_increaseInWebSpending_cache(Integer cust_sk)
    throws SQLException {
      if (cust_sk == null) {
        return null;}
    if (cache.containsKey(cust_sk)) {
      return cache.get(cust_sk);
    } else {
      Double result = evaluation(cust_sk);
      cache.put(cust_sk, result);
      return result;
    }
  }

  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static Double sudf_12_increaseInWebSpending_null(Integer cust_sk)
    throws SQLException {
    return evaluation(cust_sk);
  }
  @Function(parallel = Parallel.SAFE)
  public static Double sudf_12_increaseInWebSpending_safe(Integer cust_sk)
    throws SQLException {
    return evaluation(cust_sk);
  }
}
