package com.sampmobile.launcher.ui.adapter;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.joom.paranoid.Obfuscate;

import java.util.List;

import com.sampmobile.launcher.R;

@Obfuscate
public class RulesAdapter extends RecyclerView.Adapter<RulesAdapter.ViewHolder> {

    private final List<String[]> rules;

    static class ViewHolder extends RecyclerView.ViewHolder {
        TextView rule_name;
        TextView rule_value;

        public ViewHolder(View itemView) {
            super(itemView);
            rule_name = itemView.findViewById(R.id.rule_name);
            rule_value = itemView.findViewById(R.id.rule_value);
        }
    }

    public RulesAdapter(List<String[]> rules) {
        this.rules = rules;
    }

    @NonNull
    @Override
    public ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.layout_rule_item, parent, false);
        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull ViewHolder holder, int position) {
        String[] rule = rules.get(position);
        holder.rule_name.setText(rule[0]);
        holder.rule_value.setText(rule[1]);
    }

    @Override
    public int getItemCount() {
        return rules.size();
    }
}
