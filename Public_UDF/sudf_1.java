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

public class sudf_1 {

  private static String m_url = "jdbc:default:connection";
  private static String remove_trailingspace_regex = "\\s+$";
  private static HashMap<String, Double> cache = new HashMap<String, Double>();

  public static Double evaluation(
    String givenState,
    Double amount,
    Integer yr,
    Integer qtr
  ) throws SQLException {
    Double result = null;
    String query =
      "select sum(cs_net_paid_inc_ship_tax) from catalog_sales_history, customer, customer_address, date_dim where cs_bill_customer_sk = c_customer_sk and c_current_addr_sk = ca_address_sk and ca_state = ? and cs_net_paid_inc_ship_tax >= ? and d_date_sk = cs_sold_date_sk and d_year = ? and d_qoy = ?";
    Connection conn = DriverManager.getConnection(m_url);
    PreparedStatement stmt = conn.prepareStatement(query);
    stmt.setString(1, givenState);
    stmt.setDouble(2, amount);
    stmt.setInt(3, yr);
    stmt.setInt(4, qtr);
    ResultSet rs = stmt.executeQuery();
    if (rs.next()) {
      // non-empty result processing
      // very necessary
      // Need to deal a case wherein i_manufact is null
      result = rs.getDouble(1);
      if (result == null) {
        result = Double.valueOf(0);
      }
    }
    stmt.close();
    conn.close();
    return result;
  }

  @Function
  public static Double sudf_1_totalLargePurchases(
    String givenState,
    Double amount,
    Integer yr,
    Integer qtr
  ) throws SQLException {
    if (givenState == null || amount == null || yr == null || qtr == null) {
      return null;
    }
    Double result = evaluation(givenState, amount, yr, qtr);
    return result;
  }
  @Function(onNullInput = OnNullInput.RETURNS_NULL)
  public static Double sudf_1_totalLargePurchases_null(
    String givenState,
    Double amount,
    Integer yr,
    Integer qtr
  ) throws SQLException {
    if (givenState == null || amount == null || yr == null || qtr == null) {
      return null;
    }
    Double result = evaluation(givenState, amount, yr, qtr);
    return result;
  }

  @Function(parallel = Parallel.SAFE)
  public static Double sudf_1_totalLargePurchases_safe(
    String givenState,
    Double amount,
    Integer yr,
    Integer qtr
  ) throws SQLException {
    if (givenState == null || amount == null || yr == null || qtr == null) {
      return null;
    }
    Double result = evaluation(givenState, amount, yr, qtr);
    return result;
  }


  @Function
  public static Double sudf_1_totalLargePurchases_cache(
    String givenState,
    Double amount,
    Integer yr,
    Integer qtr
  ) throws SQLException {
    if (givenState == null || amount == null || yr == null || qtr == null) {
      return null;
    }
    String key =
      givenState + amount.toString() + yr.toString() + qtr.toString();
    if (cache.containsKey(key)) {
      return cache.get(key);
    } else {
      Double result = evaluation(givenState, amount, yr, qtr);
      cache.put(key, result);
      return result;
    }
  }
}
